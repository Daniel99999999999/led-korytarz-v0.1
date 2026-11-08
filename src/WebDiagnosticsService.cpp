#include "WebDiagnosticsService.hpp"

#include "DebugLog.hpp"
#include "DeviceIdentity.hpp"
#include "LedController.hpp"
#include "MqttService.hpp"
#include "OtaService.hpp"
#include "SettingsService.hpp"
#include "StackMonitor.hpp"
#include "WebPassword.hpp"
#include "WifiService.hpp"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/base64.h"
#include "mbedtls/platform_util.h"
#include "mdns.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr char kTag[] = "web_diagnostics";
constexpr char kHostname[] = "sterownik-led";
constexpr std::uint32_t kInternalHeapCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
std::atomic<std::uint32_t> tls_sessions_opened{0};
std::atomic<std::uint32_t> tls_sessions_closed{0};
std::atomic<std::int64_t> last_close_heap_log_us{0};

std::uint32_t activeTlsSessions(const std::uint32_t opened, const std::uint32_t closed) noexcept {
    return opened >= closed ? opened - closed : 0;
}

void onTlsSession(esp_https_server_user_cb_arg_t* const event) noexcept {
    if (event->user_cb_state == HTTPD_SSL_USER_CB_SESS_CREATE) {
        tls_sessions_opened.fetch_add(1, std::memory_order_relaxed);
    } else if (event->user_cb_state == HTTPD_SSL_USER_CB_SESS_CLOSE) {
        tls_sessions_closed.fetch_add(1, std::memory_order_relaxed);
    }
}

void onHttpsDisconnected(void* const context, const esp_event_base_t base, const std::int32_t id,
                         void* const data) noexcept {
    static_cast<void>(context);
    static_cast<void>(base);
    static_cast<void>(id);
    static_cast<void>(data);
    const std::int64_t now_us = esp_timer_get_time();
    const std::int64_t previous = last_close_heap_log_us.load(std::memory_order_relaxed);
    if (previous != 0 && now_us - previous < 60LL * 1000LL * 1000LL)
        return;
    last_close_heap_log_us.store(now_us, std::memory_order_relaxed);
    const auto opened = tls_sessions_opened.load(std::memory_order_relaxed);
    const auto closed = tls_sessions_closed.load(std::memory_order_relaxed);
    ESP_LOGI(kTag,
             "HTTPS after close: sessions=%u/%u, active=%u, free=%u B, largest=%u B, minimum=%u B",
             static_cast<unsigned>(opened), static_cast<unsigned>(closed),
             static_cast<unsigned>(activeTlsSessions(opened, closed)),
             static_cast<unsigned>(heap_caps_get_free_size(kInternalHeapCaps)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(kInternalHeapCaps)),
             static_cast<unsigned>(heap_caps_get_minimum_free_size(kInternalHeapCaps)));
}
constexpr char kSetupPage[] = R"HTML(<!doctype html><html lang="pl"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Haslo | Sterownik LED</title>
<style>:root{font-family:system-ui,sans-serif;color:#20292d;background:#f5f7f7}*{box-sizing:border-box}body{margin:0}header{background:#fff;border-bottom:1px solid #d8e0e1;padding:18px max(18px,calc((100vw - 720px)/2))}h1{font-size:23px;margin:0}main{max-width:720px;margin:38px auto;padding:0 18px}h2{font-size:20px}form{display:grid;gap:12px;max-width:380px}label{display:grid;gap:6px;font-size:14px}input{font:inherit;padding:9px;border:1px solid #aab9bc;border-radius:4px;background:#fff;min-width:0}button{font:inherit;padding:9px 14px;background:#127365;color:white;border:0;border-radius:4px;cursor:pointer}button:disabled{opacity:.5}#message{min-height:24px;color:#a45420;margin-top:16px}</style></head>
<body><header><h1>Sterownik LED</h1></header><main><h2>Ustaw haslo administratora</h2>
<form id="form"><label>Nowe haslo<input id="password" type="password" autocomplete="new-password" minlength="12" maxlength="64" required></label>
<label>Powtorz haslo<input id="confirm" type="password" autocomplete="new-password" minlength="12" maxlength="64" required></label>
<button type="submit">Zapisz haslo</button></form><div id="message" role="status"></div></main>
<script>document.getElementById('form').onsubmit=async e=>{e.preventDefault();const p=document.getElementById('password').value;const c=document.getElementById('confirm').value;const m=document.getElementById('message');if(p!==c){m.textContent='Hasla sa rozne';return}if(!/^[\x21-\x7e]{12,64}$/.test(p)||p==='admin'){m.textContent='Haslo: 12-64 widoczne znaki ASCII';return}const b=e.target.querySelector('button');b.disabled=true;try{const r=await fetch('/api/password',{method:'POST',headers:{'Content-Type':'text/plain','X-Device-Action':'1'},body:p});m.textContent=r.ok?'Zapis w toku. Za kilka sekund zaloguj sie nowym haslem.':'Nie zapisano: HTTP '+r.status;if(r.ok){e.target.reset();setTimeout(()=>location.replace('/'),6000)}}catch{m.textContent='Brak polaczenia'}finally{b.disabled=false}}</script></body></html>)HTML";
constexpr char kPage[] = R"HTML(<!doctype html>
<html lang="pl"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Sterownik LED</title><style>
:root{font-family:system-ui,sans-serif;color:#20292d;background:#f5f7f7}*{box-sizing:border-box}body{margin:0}header{background:#fff;border-bottom:1px solid #d8e0e1;padding:18px max(18px,calc((100vw - 1120px)/2));display:flex;align-items:center;justify-content:space-between;gap:16px}h1{font-size:23px;margin:0;font-weight:650}h2{font-size:17px;margin:0 0 14px}main{max-width:1120px;margin:auto;padding:24px 18px 50px}.meta{font-size:13px;color:#657276}.summary{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:24px;border-bottom:1px solid #d8e0e1;padding:0 0 22px}.summary section{min-width:0}.metric{display:grid;grid-template-columns:120px minmax(0,1fr);gap:7px;font-size:14px;margin:7px 0}.metric span{color:#657276}.metric strong{font-weight:550;overflow-wrap:anywhere}.ok{color:#127365}.warn{color:#a45420}.bad{color:#b53e37}.grid{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:32px;margin-top:25px}.rows{border-top:1px solid #d8e0e1}.row{display:grid;grid-template-columns:110px minmax(0,1fr) 85px;align-items:center;gap:14px;min-height:42px;border-bottom:1px solid #e2e8e9;font-size:14px}.track{height:7px;background:#e1e7e8}.fill{height:100%;background:#157e71}.off .fill{background:#9aa5a8}.muted{color:#687578}button{border:1px solid #becbcd;background:white;color:#233b40;padding:8px 12px;border-radius:4px;cursor:pointer;font:inherit}button:hover{border-color:#157e71}button:disabled{opacity:.5;cursor:default}.toolbar{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.logs{margin-top:30px;border-top:1px solid #d8e0e1;padding-top:20px}.logs-head{display:flex;justify-content:space-between;align-items:center;gap:10px}pre{background:#182225;color:#d9e9e5;padding:14px;min-height:180px;max-height:390px;overflow:auto;font:12px/1.55 ui-monospace,Consolas,monospace;white-space:pre-wrap;overflow-wrap:anywhere;border-radius:4px}#message{font-size:13px;min-height:18px;color:#a45420}@media(max-width:720px){header{padding:14px 18px}.summary,.grid{grid-template-columns:1fr;gap:18px}.grid{margin-top:20px}.row{grid-template-columns:92px minmax(0,1fr) 70px}}
</style></head><body><header><div><h1>Sterownik LED</h1><div id="version" class="meta">Ladowanie...</div></div><div id="connection" class="meta">Laczenie</div></header><main><div class="summary"><section><h2>Sieć</h2><div id="wifi"></div></section><section><h2>MQTT</h2><div id="mqtt"></div></section><section><h2>System</h2><div id="system"></div></section></div><div class="grid"><section><h2>Kanały LED</h2><div id="channels" class="rows"></div></section><section><h2>Czujniki</h2><div id="sensors" class="rows"></div><div class="toolbar" style="margin-top:14px"><button id="wifi-scan" title="Skanuj sieci Wi-Fi">Skan Wi-Fi</button><button id="temp-scan" title="Skanuj magistralę 1-Wire">Skan temperatury</button></div></section></div><section class="logs"><h2>Podzespoly I2C</h2><p class="meta">Ostatnie obserwacje komunikacji; nie jest to ciagly test obecnosci. Bateria DS1307 nie jest mierzona.</p><div id="hardware"></div></section><section class="logs"><h2>Diagnostyka stosow</h2><p class="meta">Minimum wolnego stosu od startu. Brak pomiaru nie oznacza OK. Zadania zdarzeniowe mierzone po operacji.</p><div id="stacks"></div></section><section class="logs"><div class="logs-head"><h2>Logi</h2><div class="toolbar"><button id="refresh" title="Odśwież status">Odśwież</button><button id="clear" title="Wyczyść widok logów">Wyczyść widok</button></div></div><div id="message" role="status"></div><pre id="log" aria-label="Logi urządzenia"></pre></section></main><script>
const $=id=>document.getElementById(id);let cursor=0,busy=false;
const value=(v,f='-')=>v===null||v===undefined||v===''?f:String(v);
function metric(name,v){const d=document.createElement('div');d.className='metric';const a=document.createElement('span'),b=document.createElement('strong');a.textContent=name;b.textContent=value(v);d.append(a,b);return d}
function block(id,items){const n=$(id);n.replaceChildren(...items.map(x=>metric(x[0],x[1])))}
function row(name,v,percent){const d=document.createElement('div');d.className='row';const a=document.createElement('span'),b=document.createElement('div'),c=document.createElement('strong');a.textContent=name;c.textContent=v;if(percent===null){b.textContent='';}else{b.className='track';const f=document.createElement('div');f.className='fill';f.style.width=Math.max(0,Math.min(100,percent))+'%';b.append(f);if(!percent)d.classList.add('off')}d.append(a,b,c);return d}
async function status(){
const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);const s=await r.json();
$('version').textContent='Firmware '+value(s.version)+' · '+value(s.uptime_s)+' s';
$('connection').textContent=s.wifi.state==='connected'?'Połączono':'Brak połączenia';
block('wifi',[['Stan',s.wifi.state],['SSID',s.wifi.ssid],['IPv4',s.wifi.ip],['Sygnał',s.wifi.rssi===null?'-':s.wifi.rssi+' dBm'],['NTP',s.wifi.time]]);
block('mqtt',[['Stan',s.mqtt.state],['Broker',s.mqtt.uri],['Połączenia',s.mqtt.connect_count],['Komendy',s.mqtt.accepted+'/'+s.mqtt.rejected],['Birth',s.mqtt.birth_queued?'w kolejce':'oczekuje'],['Subskrypcje',s.mqtt.subscriptions_done+'/'+s.mqtt.subscriptions_total+(s.mqtt.subscriptions_ready?' OK':'')],['Discovery',s.mqtt.discovery_confirmed?'PUBACK':(s.mqtt.discovery_queued?'w kolejce':'oczekuje')],['JSON',s.mqtt.discovery_bytes+' B'],['Outbox',s.mqtt.outbox_bytes+' B'],['Blad sync',s.mqtt.sync_error],['Kolejka',s.mqtt.backlog?'oczekuje':'pusta']]);
block('system',[['OTA',s.ota.state],['Postęp',s.ota.progress+'%'],['EEPROM',s.settings?s.settings.eeprom:'-'],['Rekord',s.settings?s.settings.record:'-']]);
const h=s.hardware;
block('hardware',h?[
['I2C kontroler',h.bus_initialized?'gotowy':'niezainicjalizowany'],
['LCD',h.lcd_ready?'zainicjalizowany':'niegotowy'],
['LCD ostatni blad',h.lcd_error],['LCD ramki / niezgodnosci DDRAM',h.frames+' / '+h.mismatches],
['LCD kontrole / ponowienia',h.checks+' / '+h.lcd_retries],
['DS1307 komunikacja',!h['nvram.enabled']?'wylaczona':!h['nvram.io.attempted']?'brak proby':h['nvram.io.responding']?'ostatnia proba OK':'blad'],
['DS1307 wiek ostatniej proby',h['nvram.io.attempted']?Math.max(0,s.uptime_s-h['nvram.io.last_io_s'])+' s':'-'],
['NVRAM rekord',h['nvram.record_valid']?'poprawny':'brak poprawnego'],
['NVRAM zgodny z RAM',h['nvram.runtime_matches']?'tak':'nie'],
['NVRAM zapis oczekuje',h['nvram.save_pending']?'tak':'nie'],
['NVRAM potwierdzone zapisy',h['nvram.saves']],['NVRAM ostatni blad',h['nvram.last_error']],
['KEY1 maska RAM / NVRAM (3 = 1+2)',h['nvram.ram_key1']+' / '+h['nvram.saved_key1']],
['EEPROM ostatni blad',s.settings?s.settings.error:'wylaczony'],
['INA228',s.ina?(s.ina.present?'wykryty':'niewykryty')+'; '+s.ina.error:'wylaczony']
]:[['Status','Brak pomiaru']]);
$('stacks').replaceChildren(...(s.stacks||[]).map(x=>{
const d=document.createElement('div');d.className=x.level==='critical'?'bad':x.level==='warning'?'warn':'';
d.textContent=x.task+': '+(x.measured?x.minimum+' B wolne / '+x.budget+' B, szczyt '+x.used_peak+' B; '+x.level+'; pomiar '+Math.max(0,s.uptime_s-x.sampled_at)+' s temu'+(x.finished?' (zakonczone)':''):'brak pomiaru');return d}));
$('channels').replaceChildren(...s.channels.map((x,i)=>row('Kanał '+(i+1),x.on?x.brightness+'%':'OFF',x.on?x.brightness:0)));
const sensor=[];s.pir.forEach((x,i)=>sensor.push(row('PIR '+(i+1),x?'ruch':'spokój',null)));
if(s.ina){sensor.push(row('Prąd',s.ina.valid?s.ina.current_ma+' mA':'brak pomiaru',null));sensor.push(row('Napięcie',s.ina.valid?(s.ina.voltage_mv/1000).toFixed(2)+' V':'-',null));sensor.push(row('Moc',s.ina.valid?s.ina.power_mw+' mW':'-',null))}
if(s.temperature){s.temperature.roles.forEach(x=>sensor.push(row(x.name,x.valid?(x.millicelsius/1000).toFixed(1)+' °C':'-',null)))}
$('sensors').replaceChildren(...sensor);$('temp-scan').hidden=!s.temperature}
async function logs(){const r=await fetch('/api/logs?after='+cursor,{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);const data=await r.json();let text=data.gap?'[starsze logi nadpisane]\n':'';for(const item of data.entries){text+=item.text.replace(/\r/g,'');cursor=item.sequence}if(text){const view=$('log');view.textContent=(view.textContent+text).slice(-32768);view.scrollTop=view.scrollHeight}}async function poll(){if(busy)return;busy=true;try{await status();await logs();$('message').textContent=''}catch(e){$('message').textContent='Nie można pobrać danych: '+e.message}finally{busy=false}}async function command(path){try{const r=await fetch(path,{method:'POST',headers:{'X-Device-Action':'1'}});$('message').textContent=r.ok?'Skan zlecony':('Polecenie odrzucone: HTTP '+r.status)}catch(e){$('message').textContent='Brak połączenia'}}$('wifi-scan').onclick=()=>command('/api/scan/wifi');$('temp-scan').onclick=()=>command('/api/scan/temperature');$('refresh').onclick=poll;$('clear').onclick=()=>{$('log').textContent=''};poll();setInterval(poll,2000);
const passwordLink=document.createElement('a');passwordLink.href='/password';passwordLink.textContent='Zmien haslo';passwordLink.className='meta';passwordLink.style.display='block';document.querySelector('header div:first-child').append(passwordLink);
</script></body></html>)HTML";

class JsonOwner final {
  public:
    explicit JsonOwner(cJSON* value) noexcept : value_(value) {}
    ~JsonOwner() {
        cJSON_Delete(value_);
    }
    JsonOwner(const JsonOwner&) = delete;
    JsonOwner& operator=(const JsonOwner&) = delete;
    cJSON* get() const noexcept {
        return value_;
    }

  private:
    cJSON* value_;
};

const char* wifiState(WifiService::State state) noexcept {
    switch (state) {
    case WifiService::State::unavailable:
        return "unavailable";
    case WifiService::State::unconfigured:
        return "unconfigured";
    case WifiService::State::ready:
        return "ready";
    case WifiService::State::connecting:
        return "connecting";
    case WifiService::State::connected:
        return "connected";
    case WifiService::State::disconnected:
        return "disconnected";
    }
    return "unknown";
}

const char* mqttState(MqttService::State state) noexcept {
    switch (state) {
    case MqttService::State::unavailable:
        return "unavailable";
    case MqttService::State::unconfigured:
        return "unconfigured";
    case MqttService::State::ready:
        return "ready";
    case MqttService::State::connecting:
        return "connecting";
    case MqttService::State::connected:
        return "connected";
    case MqttService::State::disconnected:
        return "disconnected";
    case MqttService::State::authentication_error:
        return "authentication error";
    }
    return "unknown";
}

bool addString(cJSON* object, const char* key, const char* value) noexcept {
    return cJSON_AddStringToObject(object, key, value) != nullptr;
}
bool addNumber(cJSON* object, const char* key, double value) noexcept {
    return cJSON_AddNumberToObject(object, key, value) != nullptr;
}
bool addBool(cJSON* object, const char* key, bool value) noexcept {
    return cJSON_AddBoolToObject(object, key, value) != nullptr;
}
bool addNull(cJSON* object, const char* key) noexcept {
    return cJSON_AddNullToObject(object, key) != nullptr;
}
esp_err_t unavailable(httpd_req_t* request, const char* message) noexcept {
    httpd_resp_set_status(request, "503 Service Unavailable");
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(request, message);
}
} // namespace

WebDiagnosticsService::WebDiagnosticsService(WifiService& wifi, MqttService& mqtt, OtaService& ota,
                                             LedController& controller,
                                             SettingsService& settings) noexcept
    : wifi_(wifi), mqtt_(mqtt), ota_(ota), controller_(controller), settings_(settings) {}

esp_err_t WebDiagnosticsService::initialize() noexcept {
    if (initialized_)
        return ESP_ERR_INVALID_STATE;
    const esp_err_t result = tls_.initialize();
    if (result != ESP_OK)
        return result;
    refreshSessionToken();
    std::printf("Panel HTTPS: https://%s.local lub https://<IPv4 ESP32>\n", kHostname);
    std::printf("Pierwsze logowanie: admin/admin; zmiana hasla obowiazkowa.\n");
    std::fflush(stdout);
    initialized_ = true;
    return ESP_OK;
}

void WebDiagnosticsService::refreshSessionToken() noexcept {
    std::array<std::uint8_t, 16> token{};
    esp_fill_random(token.data(), token.size());
    for (std::size_t index = 0; index < token.size(); ++index)
        std::snprintf(session_token_.data() + index * 2U, 3, "%02x", token[index]);
    std::snprintf(session_cookie_.data(), session_cookie_.size(),
                  "sid=%s; Path=/; Secure; HttpOnly; SameSite=Strict", session_token_.data());
    token.fill(0);
}

void WebDiagnosticsService::logHeapStatus() noexcept {
    constexpr std::int64_t kIntervalUs = 60LL * 1000LL * 1000LL;
    const std::int64_t now_us = esp_timer_get_time();
    if (last_heap_log_us_ != 0 && now_us - last_heap_log_us_ < kIntervalUs)
        return;
    const auto opened = tls_sessions_opened.load(std::memory_order_relaxed);
    const auto closed = tls_sessions_closed.load(std::memory_order_relaxed);
    ESP_LOGI(kTag, "HTTPS RAM: sessions=%u/%u, active=%u, free=%u B, largest=%u B, minimum=%u B",
             static_cast<unsigned>(opened), static_cast<unsigned>(closed),
             static_cast<unsigned>(activeTlsSessions(opened, closed)),
             static_cast<unsigned>(heap_caps_get_free_size(kInternalHeapCaps)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(kInternalHeapCaps)),
             static_cast<unsigned>(heap_caps_get_minimum_free_size(kInternalHeapCaps)));
    last_heap_log_us_ = now_us;
}

esp_err_t WebDiagnosticsService::start() noexcept {
    if (!initialized_)
        return ESP_ERR_INVALID_STATE;
    if (server_ != nullptr)
        return ESP_OK;
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.httpd.stack_size = 12288;
    config.httpd.max_open_sockets = 1;
    config.httpd.lru_purge_enable = true;
    config.httpd.max_uri_handlers = 7;
    config.httpd.recv_wait_timeout = 3;
    config.httpd.send_wait_timeout = 3;
    config.user_cb = onTlsSession;
    config.servercert = tls_.certificate();
    config.servercert_len = tls_.certificateLength();
    config.prvtkey_pem = tls_.privateKey();
    config.prvtkey_len = tls_.privateKeyLength();
    esp_err_t result = httpd_ssl_start(&server_, &config);
    if (result != ESP_OK)
        return result;
    result = registerRoutes();
    if (result != ESP_OK) {
        httpd_ssl_stop(server_);
        server_ = nullptr;
        return result;
    }
    if (!tls_event_registered_) {
        result = esp_event_handler_instance_register(
            ESP_HTTPS_SERVER_EVENT, HTTPS_SERVER_EVENT_DISCONNECTED, onHttpsDisconnected, nullptr,
            &tls_event_instance_);
        if (result == ESP_OK)
            tls_event_registered_ = true;
        else
            ESP_LOGW(kTag, "HTTPS close RAM monitoring unavailable: %s", esp_err_to_name(result));
    }
    result = mdns_init();
    if (result == ESP_OK) {
        mdns_started_ = true;
        result = mdns_hostname_set(kHostname);
        if (result == ESP_OK)
            result = mdns_instance_name_set(device_identity::kDisplayName);
        if (result == ESP_OK)
            result = mdns_service_add(nullptr, "_https", "_tcp", 443, nullptr, 0);
        if (result != ESP_OK) {
            ESP_LOGW(kTag, "mDNS: %s; panel nadal dostepny przez IP", esp_err_to_name(result));
            mdns_free();
            mdns_started_ = false;
        }
    } else {
        ESP_LOGW(kTag, "mDNS: %s; panel nadal dostepny przez IP", esp_err_to_name(result));
    }
    ESP_LOGI(kTag, "Panel diagnostyczny aktywny: HTTPS port 443, host %s.local", kHostname);
    return ESP_OK;
}

esp_err_t WebDiagnosticsService::registerRoutes() noexcept {
    const auto add = [this](const char* uri, httpd_method_t method,
                            esp_err_t (*handler)(httpd_req_t*)) noexcept {
        httpd_uri_t route{};
        route.uri = uri;
        route.method = method;
        route.handler = handler;
        route.user_ctx = this;
        return httpd_register_uri_handler(server_, &route);
    };
    esp_err_t result = add("/", HTTP_GET, page);
    if (result == ESP_OK)
        result = add("/password", HTTP_GET, page);
    if (result == ESP_OK)
        result = add("/api/password", HTTP_POST, changePassword);
    if (result == ESP_OK)
        result = add("/api/status", HTTP_GET, status);
    if (result == ESP_OK)
        result = add("/api/logs", HTTP_GET, logs);
    if (result == ESP_OK)
        result = add("/api/scan/wifi", HTTP_POST, scanWifi);
#if LED_CONTROLLER_DS18B20_ENABLED
    if (result == ESP_OK)
        result = add("/api/scan/temperature", HTTP_POST, scanTemperature);
#endif
    return result;
}

bool WebDiagnosticsService::authorized(httpd_req_t* request, const bool allow_setup) noexcept {
    persistent_settings::WebCredential credential{};
    if (settings_.readWebCredential(credential) != ESP_OK) {
        unavailable(request, "Pamiec ustawien niedostepna");
        return false;
    }
    const std::int64_t now_us = esp_timer_get_time();
    if (session_issued_ && (session_salt_ != credential.salt || now_us >= session_deadline_us_)) {
        session_issued_ = false;
        refreshSessionToken();
    }
    if (now_us < locked_until_us_) {
        mbedtls_platform_zeroize(&credential, sizeof(credential));
        httpd_resp_set_status(request, "429 Too Many Requests");
        setResponseHeaders(request);
        httpd_resp_sendstr(request, "Zbyt wiele prob logowania");
        return false;
    }

    bool accepted = false;
    std::array<char, 256> cookies{};
    const std::size_t cookie_length = httpd_req_get_hdr_value_len(request, "Cookie");
    if (session_issued_ && cookie_length > 0U && cookie_length < cookies.size() &&
        httpd_req_get_hdr_value_str(request, "Cookie", cookies.data(), cookies.size()) == ESP_OK) {
        const char* value = std::strstr(cookies.data(), "sid=");
        if (value != nullptr && std::strlen(value) >= 36U &&
            (value == cookies.data() || value[-1] == ' ' || value[-1] == ';')) {
            value += 4;
            unsigned difference = 0;
            for (std::size_t index = 0; index < 32U; ++index)
                difference |= static_cast<unsigned>(value[index] ^ session_token_[index]);
            accepted = difference == 0U && (value[32] == '\0' || value[32] == ';');
            if (accepted)
                session_deadline_us_ = now_us + 30LL * 60LL * 1000LL * 1000LL;
        }
    }
    if (!accepted) {
        std::array<char, 160> header{};
        const std::size_t header_length = httpd_req_get_hdr_value_len(request, "Authorization");
        if (header_length > 6U && header_length < header.size() &&
            httpd_req_get_hdr_value_str(request, "Authorization", header.data(), header.size()) ==
                ESP_OK &&
            std::strncmp(header.data(), "Basic ", 6U) == 0) {
            std::array<unsigned char, 96> decoded{};
            std::size_t decoded_length = 0;
            if (mbedtls_base64_decode(decoded.data(), decoded.size() - 1U, &decoded_length,
                                      reinterpret_cast<const unsigned char*>(header.data() + 6U),
                                      header_length - 6U) == 0 &&
                decoded_length < decoded.size() &&
                std::memchr(decoded.data(), 0, decoded_length) == nullptr) {
                decoded[decoded_length] = 0U;
                const char* colon = std::strchr(reinterpret_cast<const char*>(decoded.data()), ':');
                if (colon == reinterpret_cast<const char*>(decoded.data()) + 5U &&
                    std::memcmp(decoded.data(), "admin", 5U) == 0) {
                    bool matches = false;
                    accepted =
                        web_password::verify(colon + 1, credential, matches) == ESP_OK && matches;
                }
            }
            mbedtls_platform_zeroize(decoded.data(), decoded.size());
        }
        mbedtls_platform_zeroize(header.data(), header.size());
        if (accepted) {
            failed_logins_ = 0;
            session_issued_ = true;
            session_salt_ = credential.salt;
            session_deadline_us_ = now_us + 30LL * 60LL * 1000LL * 1000LL;
            httpd_resp_set_hdr(request, "Set-Cookie", session_cookie_.data());
        } else if (header_length > 0U) {
            if (++failed_logins_ >= 5U) {
                locked_until_us_ = esp_timer_get_time() + 60'000'000LL;
                failed_logins_ = 0;
            }
        }
    }
    const bool configured = credential.configured;
    mbedtls_platform_zeroize(&credential, sizeof(credential));
    if (!accepted) {
        httpd_resp_set_hdr(request, "WWW-Authenticate", "Basic realm=\"Sterownik LED\"");
        setResponseHeaders(request);
        httpd_resp_set_status(request, "401 Unauthorized");
        httpd_resp_sendstr(request, "Wymagane logowanie");
        return false;
    }
    if (!configured && !allow_setup) {
        setResponseHeaders(request);
        httpd_resp_set_status(request, "403 Forbidden");
        httpd_resp_sendstr(request, "Najpierw zmien haslo administratora");
        return false;
    }
    return true;
}
bool WebDiagnosticsService::commandAuthorized(httpd_req_t* request) noexcept {
    if (!authorized(request))
        return false;
    std::array<char, 4> guard{};
    if (httpd_req_get_hdr_value_len(request, "X-Device-Action") == 1U &&
        httpd_req_get_hdr_value_str(request, "X-Device-Action", guard.data(), guard.size()) ==
            ESP_OK &&
        guard[0] == '1')
        return true;
    httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Brak naglowka polecenia");
    return false;
}
void WebDiagnosticsService::setResponseHeaders(httpd_req_t* request) noexcept {
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(request, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(request, "Content-Security-Policy",
                       "default-src 'self'; script-src 'self' 'unsafe-inline'; "
                       "style-src 'self' 'unsafe-inline'; object-src 'none'; base-uri 'none'");
}

esp_err_t WebDiagnosticsService::page(httpd_req_t* request) {
    stack_monitor::Scope stack_sample(stack_monitor::Task::https, 12288);
    auto& service = *static_cast<WebDiagnosticsService*>(request->user_ctx);
    if (!service.authorized(request, true))
        return ESP_OK;
    const bool setup =
        !service.settings_.status().web_password_set || std::strcmp(request->uri, "/password") == 0;
    setResponseHeaders(request);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return setup ? httpd_resp_send(request, kSetupPage, sizeof(kSetupPage) - 1U)
                 : httpd_resp_send(request, kPage, sizeof(kPage) - 1U);
}

esp_err_t WebDiagnosticsService::changePassword(httpd_req_t* request) {
    stack_monitor::Scope stack_sample(stack_monitor::Task::https, 12288);
    auto& service = *static_cast<WebDiagnosticsService*>(request->user_ctx);
    if (!service.authorized(request, true))
        return ESP_OK;
    std::array<char, web_password::kMaximumLength + 1U> password{};
    std::array<char, 4> action{};
    if (httpd_req_get_hdr_value_len(request, "X-Device-Action") != 1U ||
        httpd_req_get_hdr_value_str(request, "X-Device-Action", action.data(), action.size()) !=
            ESP_OK ||
        action[0] != '1' || request->content_len < static_cast<int>(web_password::kMinimumLength) ||
        request->content_len > static_cast<int>(web_password::kMaximumLength)) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Niepoprawne zadanie");
    }
    int received = 0;
    while (received < request->content_len) {
        const int count =
            httpd_req_recv(request, password.data() + received, request->content_len - received);
        if (count <= 0) {
            mbedtls_platform_zeroize(password.data(), password.size());
            return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Niepelne haslo");
        }
        received += count;
    }
    if (std::memchr(password.data(), 0, static_cast<std::size_t>(received)) != nullptr) {
        mbedtls_platform_zeroize(password.data(), password.size());
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Niepoprawne haslo");
    }
    const esp_err_t result = service.settings_.setWebPassword(password.data());
    mbedtls_platform_zeroize(password.data(), password.size());
    if (result == ESP_ERR_INVALID_ARG)
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Haslo: 12-64 znaki ASCII");
    if (result != ESP_OK)
        return unavailable(request, "Zapis EEPROM niedostepny");
    setResponseHeaders(request);
    httpd_resp_set_status(request, "202 Accepted");
    return httpd_resp_sendstr(request, "Zapis hasla zlecony");
}

esp_err_t WebDiagnosticsService::status(httpd_req_t* request) {
    stack_monitor::Scope stack_sample(stack_monitor::Task::https, 12288);
    auto& service = *static_cast<WebDiagnosticsService*>(request->user_ctx);
    if (!service.authorized(request))
        return ESP_OK;
    service.logHeapStatus();
    JsonOwner root(cJSON_CreateObject());
    if (!root.get())
        return unavailable(request, "Brak pamieci");
    cJSON* wifi = cJSON_AddObjectToObject(root.get(), "wifi");
    cJSON* mqtt = cJSON_AddObjectToObject(root.get(), "mqtt");
    cJSON* ota = cJSON_AddObjectToObject(root.get(), "ota");
    cJSON* channels = cJSON_AddArrayToObject(root.get(), "channels");
    cJSON* pir = cJSON_AddArrayToObject(root.get(), "pir");
    if (!wifi || !mqtt || !ota || !channels || !pir)
        return unavailable(request, "Brak pamieci");
    bool ok =
        addString(root.get(), "version", esp_app_get_description()->version) &&
        addNumber(root.get(), "uptime_s", static_cast<double>(esp_timer_get_time() / 1000000));
    WifiService::Status wifi_state{};
    if (service.wifi_.tryGetStatus(wifi_state)) {
        ok = addString(wifi, "state", wifiState(wifi_state.state)) &&
             addString(wifi, "ssid", wifi_state.ssid.data()) &&
             addString(wifi, "ip", wifi_state.ip.data()) &&
             addString(wifi, "time", wifi_state.local_time.data()) && ok;
        ok = (wifi_state.state == WifiService::State::connected
                  ? addNumber(wifi, "rssi", wifi_state.rssi_dbm)
                  : addNull(wifi, "rssi")) &&
             ok;
    } else {
        ok = addString(wifi, "state", "busy") && addString(wifi, "ssid", "") &&
             addString(wifi, "ip", "") && addString(wifi, "time", "") && addNull(wifi, "rssi") &&
             ok;
    }
    MqttService::Status mqtt_state{};
    if (service.mqtt_.tryGetStatus(mqtt_state)) {
        const char* uri = mqtt_state.uri.data();
        const char* shown_uri = std::strpbrk(uri, "@?#") == nullptr ? uri : "(ukryto)";
        ok =
            addString(mqtt, "state", mqttState(mqtt_state.state)) &&
            addString(mqtt, "uri", shown_uri) &&
            addNumber(mqtt, "connect_count", mqtt_state.connect_count) &&
            addNumber(mqtt, "accepted", mqtt_state.accepted_commands) &&
            addNumber(mqtt, "rejected", mqtt_state.rejected_commands) &&
            addBool(mqtt, "backlog", mqtt_state.publish_backlog) &&
            addBool(mqtt, "birth_queued", mqtt_state.birth_queued) &&
            addBool(mqtt, "subscriptions_ready", mqtt_state.subscriptions_ready) &&
            addNumber(mqtt, "subscriptions_done", mqtt_state.subscriptions_done) &&
            addNumber(mqtt, "subscriptions_total", mqtt_state.subscriptions_total) &&
            addBool(mqtt, "discovery_queued", mqtt_state.discovery_queued) &&
            addBool(mqtt, "discovery_confirmed", mqtt_state.discovery_confirmed) &&
            addNumber(mqtt, "discovery_bytes", mqtt_state.discovery_payload_bytes) &&
            addNumber(mqtt, "outbox_bytes", mqtt_state.outbox_bytes) &&
            addString(mqtt, "sync_error", MqttService::syncErrorName(mqtt_state.last_sync_error)) &&
            ok;
    } else {
        ok = addString(mqtt, "state", "busy") && addString(mqtt, "uri", "") &&
             addNumber(mqtt, "connect_count", 0) && addNumber(mqtt, "accepted", 0) &&
             addNumber(mqtt, "rejected", 0) && addBool(mqtt, "backlog", false) &&
             addBool(mqtt, "birth_queued", false) && addBool(mqtt, "subscriptions_ready", false) &&
             addNumber(mqtt, "subscriptions_done", 0) &&
             addNumber(mqtt, "subscriptions_total", MqttSyncMachine::kSubscriptionCount) &&
             addBool(mqtt, "discovery_queued", false) &&
             addBool(mqtt, "discovery_confirmed", false) && addNumber(mqtt, "discovery_bytes", 0) &&
             addNumber(mqtt, "outbox_bytes", 0) && addString(mqtt, "sync_error", "busy") && ok;
    }
    const auto ota_state = service.ota_.status();
    ok = addString(ota, "state", OtaService::stateName(ota_state.state)) &&
         addNumber(ota, "progress", ota_state.progress_percent) &&
         addNumber(ota, "last_error", ota_state.last_error) && ok;
    MqttService::Snapshot snapshot{};
    const bool snapshot_ready = service.mqtt_.tryGetSnapshot(snapshot);
    for (std::size_t index = 0; index < MqttService::kChannelCount; ++index) {
        cJSON* item = cJSON_CreateObject();
        if (!item) {
            ok = false;
            break;
        }
        cJSON_AddItemToArray(channels, item);
        ok = addBool(item, "on", snapshot_ready && snapshot.channels[index].on) &&
             addNumber(item, "brightness",
                       snapshot_ready ? snapshot.channels[index].brightness_percent : 0) &&
             ok;
    }
    for (std::size_t index = 0; index < MqttService::kPirInputCount; ++index) {
        cJSON* item = cJSON_CreateBool(snapshot_ready && snapshot.pir_inputs[index].active);
        if (!item) {
            ok = false;
            break;
        }
        cJSON_AddItemToArray(pir, item);
    }
#if LED_CONTROLLER_INA228_ENABLED
    cJSON* ina = cJSON_AddObjectToObject(root.get(), "ina");
    if (!ina)
        ok = false;
    else {
        const auto& data = snapshot.ina228;
        ok = addBool(ina, "valid", snapshot_ready && data.valid) &&
             addBool(ina, "present", snapshot_ready && data.present) &&
             addString(ina, "error", esp_err_to_name(data.last_error)) &&
             addNumber(ina, "current_ma", data.current_milliamps) &&
             addNumber(ina, "voltage_mv", data.bus_voltage_millivolts) &&
             addNumber(ina, "power_mw", data.power_milliwatts) && ok;
    }
#endif
#if LED_CONTROLLER_DS18B20_ENABLED
    cJSON* temperature = cJSON_AddObjectToObject(root.get(), "temperature");
    cJSON* roles = temperature ? cJSON_AddArrayToObject(temperature, "roles") : nullptr;
    if (!roles)
        ok = false;
    else {
        constexpr std::array<const char*, 2> names{{"Radiator", "Obudowa"}};
        for (std::size_t index = 0; index < names.size(); ++index) {
            cJSON* role = cJSON_CreateObject();
            if (!role) {
                ok = false;
                break;
            }
            cJSON_AddItemToArray(roles, role);
            ok = addString(role, "name", names[index]) &&
                 addBool(role, "valid",
                         snapshot_ready && snapshot.temperatures.roles[index].valid) &&
                 addNumber(role, "millicelsius",
                           snapshot.temperatures.roles[index].temperature_millicelsius) &&
                 ok;
        }
    }
#endif
#if LED_CONTROLLER_AT24C08_ENABLED
    cJSON* settings = cJSON_AddObjectToObject(root.get(), "settings");
    if (!settings)
        ok = false;
    else {
        const auto data = service.settings_.status();
        ok = addString(settings, "eeprom", data.eeprom_present ? "wykryty" : "brak") &&
             addString(settings, "record", data.record_applied ? "aktywny" : "brak") &&
             addString(settings, "error", esp_err_to_name(data.last_error)) && ok;
    }
#endif
    if (!ok || !cJSON_PrintPreallocated(root.get(), service.response_buffer_.data(),
                                        static_cast<int>(service.response_buffer_.size()), false))
        return unavailable(request, "Brak pamieci");
    // Append bounded diagnostics without another cJSON tree or a large stack buffer.
    auto& response = service.response_buffer_;
    std::size_t used = std::strlen(response.data());
    if (used == 0 || response[used - 1U] != '}')
        return unavailable(request, "Blad JSON");
    --used;
    auto append = [&](const char* format, auto... args) {
        const int count =
            std::snprintf(response.data() + used, response.size() - used, format, args...);
        if (count < 0 || static_cast<std::size_t>(count) >= response.size() - used)
            return false;
        used += static_cast<std::size_t>(count);
        return true;
    };
    if (!append(",\"stacks\":["))
        return unavailable(request, "Brak miejsca na diagnostyke");
    for (unsigned i = 0; i < stack_monitor::kCount; ++i) {
        const auto task = static_cast<stack_monitor::Task>(i);
        stack_model::Record record{};
        const bool measured = stack_monitor::read(task, record) && record.valid;
        if (!append("%s{\"task\":\"%s\",\"level\":\"%s\",\"measured\":%s,\"finished\":%s,"
                    "\"budget\":%lu,\"minimum\":%lu,\"used_peak\":%lu,\"sampled_at\":%lu,\"minimum_"
                    "at\":%lu}",
                    i ? "," : "", stack_monitor::name(task), stack_model::name(record.severity()),
                    measured ? "true" : "false", record.finished ? "true" : "false",
                    static_cast<unsigned long>(record.budget),
                    static_cast<unsigned long>(record.minimum),
                    static_cast<unsigned long>(measured ? record.budget - record.minimum : 0),
                    static_cast<unsigned long>(record.sampled_at),
                    static_cast<unsigned long>(record.minimum_at)))
            return unavailable(request, "Brak miejsca na diagnostyke");
    }
    if (!append("]"))
        return unavailable(request, "Brak miejsca na diagnostyke");
    HardwareDiagnostics hardware{};
    if (!append(",\"hardware\":"))
        return unavailable(request, "Brak miejsca na diagnostyke");
    if (service.controller_.tryGetHardwareDiagnostics(hardware) && hardware.sampled) {
        if (!append("{"))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append("\"sampled\":%ld", static_cast<long>(hardware.sampled)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"sampled_at\":%ld", static_cast<long>(hardware.sampled_at)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"bus_initialized\":%ld", static_cast<long>(hardware.bus_initialized)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"lcd_ready\":%ld", static_cast<long>(hardware.lcd_ready)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"backlight\":%ld", static_cast<long>(hardware.backlight)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"lcd_error\":%ld", static_cast<long>(hardware.lcd_error)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"frames\":%ld", static_cast<long>(hardware.frames)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"last_frame_s\":%ld", static_cast<long>(hardware.last_frame_s)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"lcd_retries\":%ld", static_cast<long>(hardware.lcd_retries)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"ddram_checked\":%ld", static_cast<long>(hardware.ddram_checked)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"ddram_error\":%ld", static_cast<long>(hardware.ddram_error)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"checks\":%ld", static_cast<long>(hardware.checks)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"mismatches\":%ld", static_cast<long>(hardware.mismatches)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"last_check_s\":%ld", static_cast<long>(hardware.last_check_s)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"bus_recoveries\":%ld", static_cast<long>(hardware.bus_recoveries)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"bus_recovery_error\":%ld", static_cast<long>(hardware.bus_recovery_error)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"lcd_io.attempted\":%ld", static_cast<long>(hardware.lcd_io.attempted)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"lcd_io.responding\":%ld", static_cast<long>(hardware.lcd_io.responding)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"lcd_io.error\":%ld", static_cast<long>(hardware.lcd_io.error)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"lcd_io.last_io_s\":%ld", static_cast<long>(hardware.lcd_io.last_io_s)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"nvram.enabled\":%ld", static_cast<long>(hardware.nvram.enabled)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"nvram.io.attempted\":%ld", static_cast<long>(hardware.nvram.io.attempted)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"nvram.io.responding\":%ld",
                    static_cast<long>(hardware.nvram.io.responding)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"nvram.io.error\":%ld", static_cast<long>(hardware.nvram.io.error)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"nvram.io.last_io_s\":%ld", static_cast<long>(hardware.nvram.io.last_io_s)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"nvram.record_valid\":%ld", static_cast<long>(hardware.nvram.record_valid)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"nvram.runtime_matches\":%ld",
                    static_cast<long>(hardware.nvram.runtime_matches)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"nvram.save_pending\":%ld", static_cast<long>(hardware.nvram.save_pending)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"nvram.restore_queued\":%ld",
                    static_cast<long>(hardware.nvram.restore_queued)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"nvram.retry_pending\":%ld",
                    static_cast<long>(hardware.nvram.retry_pending)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"nvram.last_error\":%ld", static_cast<long>(hardware.nvram.last_error)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"nvram.sequence\":%ld", static_cast<long>(hardware.nvram.sequence)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"nvram.saves\":%ld", static_cast<long>(hardware.nvram.saves)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"nvram.runtime_revision\":%ld",
                    static_cast<long>(hardware.nvram.runtime_revision)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"nvram.slot\":%ld", static_cast<long>(hardware.nvram.slot)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"nvram.phase\":%ld", static_cast<long>(hardware.nvram.phase)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"nvram.ram_key1\":%ld", static_cast<long>(hardware.nvram.ram_key1)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append(",\"nvram.saved_key1\":%ld", static_cast<long>(hardware.nvram.saved_key1)))
            return unavailable(request, "Brak miejsca na diagnostyke");
        if (!append("}"))
            return unavailable(request, "Brak miejsca na diagnostyke");
    } else if (!append("null"))
        return unavailable(request, "Brak miejsca na diagnostyke");
    if (!append("}"))
        return unavailable(request, "Brak miejsca na diagnostyke");
    setResponseHeaders(request);
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    return httpd_resp_send(request, service.response_buffer_.data(), HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebDiagnosticsService::logs(httpd_req_t* request) {
    stack_monitor::Scope stack_sample(stack_monitor::Task::https, 12288);
    auto& service = *static_cast<WebDiagnosticsService*>(request->user_ctx);
    if (!service.authorized(request))
        return ESP_OK;
    std::array<char, 48> query{};
    std::array<char, 16> after_text{};
    std::uint32_t after = 0;
    if (httpd_req_get_url_query_str(request, query.data(), query.size()) == ESP_OK &&
        httpd_query_key_value(query.data(), "after", after_text.data(), after_text.size()) ==
            ESP_OK) {
        errno = 0;
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(after_text.data(), &end, 10);
        if (errno != 0 || end == after_text.data() || *end != '\0' || parsed > UINT32_MAX)
            return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Niepoprawny kursor");
        after = static_cast<std::uint32_t>(parsed);
    }
    JsonOwner root(cJSON_CreateObject());
    cJSON* entries = root.get() ? cJSON_AddArrayToObject(root.get(), "entries") : nullptr;
    if (!entries)
        return unavailable(request, "Brak pamieci");
    bool gap = false;
    bool ok = true;
    for (unsigned index = 0; index < 12U; ++index) {
        WebLogBuffer::Entry entry{};
        bool entry_gap = false;
        if (!DebugLog::instance().nextWebEntry(after, entry, entry_gap))
            break;
        gap = gap || entry_gap;
        cJSON* item = cJSON_CreateObject();
        if (!item) {
            ok = false;
            break;
        }
        cJSON_AddItemToArray(entries, item);
        ok = addNumber(item, "sequence", entry.sequence) &&
             addString(item, "text", entry.text.data()) && ok;
        after = entry.sequence;
    }
    ok = addBool(root.get(), "gap", gap) && ok;
    if (!ok || !cJSON_PrintPreallocated(root.get(), service.response_buffer_.data(),
                                        static_cast<int>(service.response_buffer_.size()), false))
        return unavailable(request, "Brak pamieci");
    setResponseHeaders(request);
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    return httpd_resp_send(request, service.response_buffer_.data(), HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebDiagnosticsService::scanWifi(httpd_req_t* request) {
    stack_monitor::Scope stack_sample(stack_monitor::Task::https, 12288);
    auto& service = *static_cast<WebDiagnosticsService*>(request->user_ctx);
    if (!service.commandAuthorized(request))
        return ESP_OK;
    const esp_err_t result = service.wifi_.scan();
    if (result != ESP_OK)
        return unavailable(request, "Skan Wi-Fi niedostepny");
    setResponseHeaders(request);
    return httpd_resp_sendstr(request, "Skan Wi-Fi zlecony");
}
#if LED_CONTROLLER_DS18B20_ENABLED
esp_err_t WebDiagnosticsService::scanTemperature(httpd_req_t* request) {
    stack_monitor::Scope stack_sample(stack_monitor::Task::https, 12288);
    auto& service = *static_cast<WebDiagnosticsService*>(request->user_ctx);
    if (!service.commandAuthorized(request))
        return ESP_OK;
    service.controller_.requestTemperatureScan();
    setResponseHeaders(request);
    return httpd_resp_sendstr(request, "Skan temperatury zlecony");
}
#endif
