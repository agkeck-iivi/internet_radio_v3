#include "web_server.h"
#include "app_config.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "ir_remote.h"
#include "pcm5122_driver.h"
#include "station_data.h"
#include "board.h"
#include <stdlib.h>
#include <sys/param.h>

extern audio_board_handle_t board_handle;

static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

/* Handler for GET /api/stations */
static esp_err_t api_stations_get_handler(httpd_req_t *req) {
  char *json_str = get_stations_json();
  if (json_str == NULL) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
  free(json_str);
  return ESP_OK;
}

/* Handler for POST /api/stations */
static esp_err_t api_stations_post_handler(httpd_req_t *req) {
  int total_len = req->content_len;
  int cur_len = 0;
  char *content = malloc(total_len + 1);

  if (!content) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  int received = 0;
  while (cur_len < total_len) {
    received = httpd_req_recv(req, content + cur_len, total_len - cur_len);
    if (received <= 0) {
      if (received == HTTPD_SOCK_ERR_TIMEOUT) {
        continue;
      }
      free(content);
      return ESP_FAIL;
    }
    cur_len += received;
  }
  content[total_len] = '\0';

  if (update_stations_from_json(content) == 0) {
    save_station_data();
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
  } else {
    httpd_resp_send_500(req);
  }

  free(content);
  return ESP_OK;
}

/* Handler for GET /api/config */
static esp_err_t api_config_get_handler(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "analog_attenuation",
                          g_runtime_config.analog_attenuation);
  cJSON_AddNumberToObject(root, "digital_attenuation",
                          g_runtime_config.digital_attenuation);
  cJSON_AddNumberToObject(root, "power_save_mode",
                          g_runtime_config.power_save_mode);
  cJSON_AddNumberToObject(root, "light_sleep_delay_ms",
                          g_runtime_config.light_sleep_delay_ms);
  cJSON_AddNumberToObject(root, "deep_sleep_delay_ms",
                          g_runtime_config.deep_sleep_delay_ms);
  cJSON_AddBoolToObject(root, "ir_is_enabled", g_runtime_config.ir_is_enabled);

  char *json_str = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);

  cJSON_Delete(root);
  free(json_str);
  return ESP_OK;
}

/* Handler for POST /api/config */
static esp_err_t api_config_post_handler(httpd_req_t *req) {
  int total_len = req->content_len;
  char *content = malloc(total_len + 1);
  if (!content) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  int cur_len = 0;
  int received = 0;
  while (cur_len < total_len) {
    received = httpd_req_recv(req, content + cur_len, total_len - cur_len);
    if (received <= 0) {
      if (received == HTTPD_SOCK_ERR_TIMEOUT)
        continue;
      free(content);
      return ESP_FAIL;
    }
    cur_len += received;
  }
  content[total_len] = '\0';

  cJSON *root = cJSON_Parse(content);
  if (root) {
    cJSON *item;
    bool ir_was_enabled = g_runtime_config.ir_is_enabled;

    if ((item = cJSON_GetObjectItem(root, "analog_attenuation")))
      g_runtime_config.analog_attenuation = (pcm5122_analog_atten_t)item->valueint;
    if ((item = cJSON_GetObjectItem(root, "digital_attenuation")))
      g_runtime_config.digital_attenuation = (pcm5122_digital_atten_t)item->valueint;
    if ((item = cJSON_GetObjectItem(root, "power_save_mode")))
      g_runtime_config.power_save_mode = (power_save_mode_t)item->valueint;
    if ((item = cJSON_GetObjectItem(root, "light_sleep_delay_ms")))
      g_runtime_config.light_sleep_delay_ms = item->valueint;
    if ((item = cJSON_GetObjectItem(root, "deep_sleep_delay_ms")))
      g_runtime_config.deep_sleep_delay_ms = item->valueint;
    if ((item = cJSON_GetObjectItem(root, "ir_is_enabled")))
      g_runtime_config.ir_is_enabled = cJSON_IsTrue(item);

    save_app_config();

    // Immediate application
    pcm5122_apply_analog_attenuation();
    int current_vol = 0;
    if (board_handle && board_handle->audio_hal) {
      audio_hal_get_volume(board_handle->audio_hal, &current_vol);
      audio_hal_set_volume(board_handle->audio_hal, current_vol);
    }

    if (g_runtime_config.ir_is_enabled != ir_was_enabled) {
      if (g_runtime_config.ir_is_enabled) {
        ESP_LOGI(TAG, "IR enabled at runtime. Sending ON signal.");
        ir_remote_turn_audio_on();
      } else {
        ESP_LOGI(TAG, "IR disabled at runtime. Sending OFF signal.");
        ir_remote_turn_audio_off();
      }
    }

    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    cJSON_Delete(root);
  } else {
    httpd_resp_send_500(req);
  }

  free(content);
  return ESP_OK;
}

/* Handler for GET / (Root) - Landing Page */
static esp_err_t root_get_handler(httpd_req_t *req) {
  const char *html_response =
      "<!DOCTYPE html><html><head><meta name='viewport' "
      "content='width=device-width, initial-scale=1.0'>"
      "<style>body{font-family:'Inter',sans-serif;margin:0;padding:0;"
      "display:flex;flex-direction:column;align-items:center;"
      "justify-content:center;min-height:100vh;"
      "background:linear-gradient(135deg, #141e30, #243b55);color:white;}"
      "h1{margin-bottom:30px;text-shadow: 0 4px 6px rgba(0,0,0,0.3);}"
      ".btn{display:inline-block;padding:18px "
      "35px;margin:15px;background:rgba(255,255,255,0.1);color:white;"
      "text-decoration:none;border-radius:12px;font-size:1.2em;font-weight:600;"
      "backdrop-filter:blur(10px);border:1px solid rgba(255,255,255,0.2);"
      "box-shadow:0 8px 32px 0 rgba(31,38,135,0.37);transition:all 0.3s ease;}"
      ".btn:hover{background:rgba(255,255,255,0.2);transform:translateY(-5px);}"
      "</style>"
      "<link href='https://fonts.googleapis.com/css2?family=Inter:wght@400;600&display=swap' rel='stylesheet'>"
      "<title>Radio Manager</title></head>"
      "<body><h1>Radio Manager</h1>"
      "<a href='/stations' class='btn'>Edit Stations</a>"
      "<a href='/config' class='btn'>Configuration</a>"
      "</body></html>";

  httpd_resp_send(req, html_response, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

/* Handler for GET /stations - Station Editor */
static esp_err_t stations_page_handler(httpd_req_t *req) {
  const char *html_response =
      "<!DOCTYPE html><html><head><meta name='viewport' "
      "content='width=device-width, initial-scale=1.0'>"
      "<title>Edit Stations</title>"
      "<style>"
      "body{font-family:'Inter',sans-serif;background:linear-gradient(135deg, #0f2027, #203a43, #2c5364);"
      "color:white;padding:20px;min-height:100vh;margin:0;}"
      ".glass{background:rgba(255,255,255,0.05);backdrop-filter:blur(10px);"
      "border-radius:16px;border:1px solid rgba(255,255,255,0.1);padding:20px;"
      "box-shadow:0 8px 32px 0 rgba(0,0,0,0.37);}"
      ".grid-container{display:grid;grid-template-columns:2em 5em 12em 1fr 6em "
      "3em;gap:10px;align-items:center;min-width:700px;}"
      "@media(max-width: 800px) { "
      ".grid-container{display:flex;flex-direction:column;min-width:auto;} "
      ".header-row{display:none;} "
      ".station-row{display:flex;flex-wrap:wrap;gap:10px;margin-bottom:15px;padding:15px;background:rgba(255,255,255,0.05);border-radius:10px;} }"
      ".header-row{display:contents;font-weight:bold;color:#ccc;}"
      ".header-row span{padding:10px 0;border-bottom:1px solid rgba(255,255,255,0.1);}"
      ".station-row{display:contents;}"
      "input,select{background:rgba(0,0,0,0.2);color:white;border:1px solid rgba(255,255,255,0.2);"
      "border-radius:8px;padding:8px;font-size:0.9em;width:100%;box-sizing:border-box;outline:none;}"
      "input:focus{border-color:#3498db;}"
      "option{background:#2c3e50;color:white;}"
      ".btn{padding:10px 20px;background:#2ecc71;color:white;border:none;border-radius:8px;"
      "cursor:pointer;font-weight:600;transition:all 0.2s;}"
      ".btn:hover{background:#27ae60;}"
      ".btn-del{background:#e74c3c;padding:5px 12px;}"
      ".btn-del:hover{background:#c0392b;}"
      ".homelink{display:inline-block;margin-bottom:20px;color:#3498db;text-decoration:none;font-weight:600;}"
      ".handle{cursor:grab;font-size:1.4em;color:#aaa;user-select:none;text-align:center;}"
      "</style>"
      "<link href='https://fonts.googleapis.com/css2?family=Inter:wght@400;600&display=swap' rel='stylesheet'>"
      "</head><body>"
      "<a href='/' class='homelink'>&larr; Back to Home</a>"
      "<div class='glass'>"
      "<h3>Edit Stations</h3>"
      "<div style='overflow-x:auto;'>"
      "<div class='grid-container'>"
      "  <div class='header-row'><span></span><span>Call</span><span>Origin</span><span>URI</span><span>Type</span><span></span></div>"
      "  <div id='container' style='display:contents;'></div>"
      "</div>"
      "</div>"
      "<div style='margin-top:25px;'>"
      "<button class='btn' onclick='addStation()'>+ Add Station</button> "
      "<button class='btn' onclick='saveStations()' style='background:#3498db'>Save Changes</button>"
      "</div>"
      "</div>"
      "<script>"
      "let stations=[]; let dragSrcIx = null;"
      "async function fetchStations(){const r=await fetch('/api/stations');stations=await r.json();render();}"
      "function render(){"
      "  const c=document.getElementById('container');c.innerHTML='';"
      "  stations.forEach((s,i)=>{"
      "    const div=document.createElement('div');div.className='station-row';"
      "    div.innerHTML=`<div class='handle' draggable='true' ondragstart='dragStart(event,${i})' ondragover='dragOver(event)' ondrop='drop(event,${i})'>&#9776;</div>"
      "      <div><input value='${s.call_sign}' onchange='stations[${i}].call_sign=this.value' maxlength='4'></div>"
      "      <div><input value='${s.origin}' onchange='stations[${i}].origin=this.value' maxlength='20'></div>"
      "      <div><input value='${s.uri}' onchange='stations[${i}].uri=this.value'></div>"
      "      <div><select onchange='stations[${i}].codec=parseInt(this.value)'>"
      "        <option value='0' ${s.codec==0?'selected':''}>MP3</option><option value='1' ${s.codec==1?'selected':''}>AAC</option>"
      "        <option value='2' ${s.codec==2?'selected':''}>OGG</option><option value='3' ${s.codec==3?'selected':''}>FLAC</option>"
      "      </select></div>"
      "      <div style='text-align:center'><button class='btn btn-del' onclick='removeStation(${i})'>&times;</button></div>`;"
      "    c.appendChild(div);"
      "  });"
      "}"
      "function dragStart(e,i){dragSrcIx=i;e.dataTransfer.setData('text/plain',i);}"
      "function dragOver(e){e.preventDefault();return false;}"
      "function drop(e,i){e.stopPropagation();if(dragSrcIx!==null && dragSrcIx!=i){"
      "  const item=stations[dragSrcIx]; stations.splice(dragSrcIx,1); let target=i; if(dragSrcIx<i)target--; stations.splice(target,0,item); render();"
      "} return false;}"
      "function addStation(){stations.push({call_sign:'',origin:'',uri:'',codec:1});render();}"
      "function removeStation(i){if(confirm('Delete station?')){stations.splice(i,1);render();}}"
      "async function saveStations(){"
      "  const r=await fetch('/api/stations',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(stations)});"
      "  if(r.ok)alert('Success!');else alert('Error!');"
      "}"
      "fetchStations();</script></body></html>";

  httpd_resp_send(req, html_response, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

/* Handler for GET /config - Configuration Page */
static esp_err_t config_page_handler(httpd_req_t *req) {
  const char *html_response =
      "<!DOCTYPE html><html><head><meta name='viewport' "
      "content='width=device-width, initial-scale=1.0'>"
      "<title>Configuration</title>"
      "<style>"
      "body{font-family:'Inter',sans-serif;background:linear-gradient(135deg, #2c3e50, #000000);color:white;margin:0;padding:20px;min-height:100vh;display:flex;flex-direction:column;align-items:center;}"
      ".glass{max-width:500px;width:100%;background:rgba(255,255,255,0.03);backdrop-filter:blur(15px);border-radius:24px;border:1px solid rgba(255,255,255,0.1);padding:40px;box-shadow:0 20px 50px rgba(0,0,0,0.5);box-sizing:border-box;}"
      "h2{margin-top:0;font-weight:600;letter-spacing:-0.5px;margin-bottom:30px;}"
      ".field{margin-bottom:25px;}"
      "label{display:block;margin-bottom:8px;font-size:0.85em;color:#aaa;text-transform:uppercase;letter-spacing:1px;font-weight:600;}"
      ".tooltip{position:relative;display:inline-block;cursor:pointer;margin-left:5px;color:#3498db;text-transform:none;vertical-align:middle;}"
      ".tooltip .tip{visibility:hidden;width:240px;background:#333;color:#fff;text-align:left;border-radius:8px;padding:12px;position:absolute;z-index:1;bottom:150%;left:50%;margin-left:-120px;opacity:0;transition:opacity 0.3s;font-size:0.85rem;font-weight:400;line-height:1.4;box-shadow:0 10px 30px rgba(0,0,0,0.5);border:1px solid rgba(255,255,255,0.1);pointer-events:none;}"
      ".tooltip:hover .tip{visibility:visible;opacity:1;}"
      "input,select{width:100%;background:rgba(255,255,255,0.05);border:1px solid rgba(255,255,255,0.2);color:white;padding:12px;border-radius:12px;font-size:1em;outline:none;transition:border-color 0.2s;box-sizing:border-box;}"
      "input:focus,select:focus{border-color:#3498db;}"
      "option{background:#2c3e50;color:white;}"
      ".btn{width:100%;padding:15px;background:#3498db;color:white;border:none;border-radius:12px;font-size:1.1em;font-weight:600;cursor:pointer;margin-top:10px;transition:background 0.2s;}"
      ".btn:hover{background:#2980b9;}"
      ".homelink{align-self:flex-start;margin-bottom:20px;color:#3498db;text-decoration:none;font-weight:600;}"
      ".info-section{margin-top:30px;padding-top:25px;border-top:1px solid rgba(255,255,255,0.1);font-size:0.9em;color:#ccc;line-height:1.6;}"
      ".info-section h3{font-size:1em;color:#fff;margin-bottom:12px;text-transform:uppercase;letter-spacing:1px;}"
      ".info-section ul{padding-left:18px;margin-bottom:15px;}"
      ".info-section li{margin-bottom:8px;}"
      "</style>"
      "<link href='https://fonts.googleapis.com/css2?family=Inter:wght@400;600&display=swap' rel='stylesheet'>"
      "</head><body>"
      "<a href='/' class='homelink'>&larr; Back</a>"
      "<div class='glass'>"
      "<h2>Configuration</h2>"
      "<div id='config-form'>"
      "  <div class='field'><label>Analog Attenuation<span class='tooltip'>(i)<span class='tip'>Use -6dB here first to reduce output. This affects the hardware gain stage for cleaner sound at lower levels.</span></span></label><select id='anlgAttn'><option value='0'>0dB (2Vrms)</option><option value='1'>-6dB (1Vrms)</option></select></div>"
      "  <div class='field'><label>Digital Attenuation<span class='tooltip'>(i)<span class='tip'>Use additional attenuation here if needed. This scales the digital signal before reaching the DAC.</span></span></label><select id='digiAttn'>"
      "    <option value='0'>0 dB</option><option value='6'>-3 dB</option><option value='12'>-6 dB</option><option value='18'>-9 dB</option><option value='24'>-12 dB</option><option value='30'>-15 dB</option><option value='36'>-18 dB</option><option value='42'>-21 dB</option><option value='48'>-24 dB</option>"
      "  </select></div>"
      "  <div class='field'><label>Power Save Mode<span class='tooltip'>(i)<span class='tip'>Estimated power and annual cost:<br>- <b>None:</b> 190mA ~$1.25/yr<br>- <b>Light:</b> 18mA ~$0.12/yr<br>- <b>Deep:</b> ~8mA ~$0.05/yr</span></span></label><select id='pwrSave'><option value='0'>None</option><option value='1'>Light Sleep Only</option><option value='2'>Light &rarr; Deep Sleep</option></select></div>"
      "  <div class='field'><label>Light Sleep Delay (seconds)<span class='tooltip'>(i)<span class='tip'>Default: 1200s (20 mins)</span></span></label><input type='number' id='lightDly'></div>"
      "  <div class='field'><label>Deep Sleep Delay (seconds)<span class='tooltip'>(i)<span class='tip'>Default: 7200s (2 hours)</span></span></label><input type='number' id='deepDly'></div>"
      "  <div class='field' style='display:flex;align-items:center;'><label style='margin:0;flex:1'>Enable IR Remote</label><input type='checkbox' id='irEn' style='width:auto'></div>"
      "  <button class='btn' onclick='saveConfig()'>Save Settings</button>"
      "</div>"
      "<div class='info-section'>"
      "  <h3>Power Saving Modes</h3>"
      "  <ul>"
      "    <li><b>None:</b> The device remains fully active at all times. Best for maximum responsiveness.</li>"
      "    <li><b>Light Sleep Only:</b> Moves to low-power Light Sleep after the <i>Light Sleep Delay</i>. Maintains network and audio state for swift wakeups.</li>"
      "    <li><b>Light &rarr; Deep Sleep:</b> Enters Light Sleep first, then transitions to Deep Sleep (powered down) after the <i>Deep Sleep Delay</i>. Best for long-term energy efficiency.</li>"
      "  </ul>"
      "</div>"
      "</div>"
      "<script>"
      "async function loadConfig(){"
      "  const r=await fetch('/api/config');const c=await r.json();"
      "  document.getElementById('anlgAttn').value=c.analog_attenuation;"
      "  document.getElementById('digiAttn').value=c.digital_attenuation;"
      "  document.getElementById('pwrSave').value=c.power_save_mode;"
      "  document.getElementById('lightDly').value=c.light_sleep_delay_ms/1000;"
      "  document.getElementById('deepDly').value=c.deep_sleep_delay_ms/1000;"
      "  document.getElementById('irEn').checked=c.ir_is_enabled;}"
      "async function saveConfig(){"
      "  const data={"
      "    analog_attenuation: parseInt(document.getElementById('anlgAttn').value),"
      "    digital_attenuation: parseInt(document.getElementById('digiAttn').value),"
      "    power_save_mode: parseInt(document.getElementById('pwrSave').value),"
      "    light_sleep_delay_ms: parseInt(document.getElementById('lightDly').value)*1000,"
      "    deep_sleep_delay_ms: parseInt(document.getElementById('deepDly').value)*1000,"
      "    ir_is_enabled: document.getElementById('irEn').checked"
      "  };"
      "  const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});"
      "  if(r.ok)alert('Settings saved and applied!');else alert('Error saving settings!');}"
      "loadConfig();</script></body></html>";
  httpd_resp_send(req, html_response, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static const httpd_uri_t api_stations_get = {.uri = "/api/stations",
                                             .method = HTTP_GET,
                                             .handler =
                                                 api_stations_get_handler,
                                             .user_ctx = NULL};

static const httpd_uri_t api_stations_post = {.uri = "/api/stations",
                                              .method = HTTP_POST,
                                              .handler =
                                                  api_stations_post_handler,
                                              .user_ctx = NULL};

static const httpd_uri_t api_config_get = {.uri = "/api/config",
                                           .method = HTTP_GET,
                                           .handler = api_config_get_handler,
                                           .user_ctx = NULL};

static const httpd_uri_t api_config_post = {.uri = "/api/config",
                                            .method = HTTP_POST,
                                            .handler = api_config_post_handler,
                                            .user_ctx = NULL};

static const httpd_uri_t root_get = {.uri = "/",
                                     .method = HTTP_GET,
                                     .handler = root_get_handler,
                                     .user_ctx = NULL};

static const httpd_uri_t stations_page_get = {.uri = "/stations",
                                              .method = HTTP_GET,
                                              .handler = stations_page_handler,
                                              .user_ctx = NULL};

static const httpd_uri_t config_page_get = {.uri = "/config",
                                            .method = HTTP_GET,
                                            .handler = config_page_handler,
                                            .user_ctx = NULL};

void start_web_server(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 12000; // Increase stack size for JSON parsing and strings

  ESP_LOGI(TAG, "Starting web server on port: '%d'", config.server_port);
  if (httpd_start(&server, &config) == ESP_OK) {
    ESP_LOGI(TAG, "Registering URI handlers");
    httpd_register_uri_handler(server, &api_stations_get);
    httpd_register_uri_handler(server, &api_stations_post);
    httpd_register_uri_handler(server, &api_config_get);
    httpd_register_uri_handler(server, &api_config_post);
    httpd_register_uri_handler(server, &root_get);
    httpd_register_uri_handler(server, &stations_page_get);
    httpd_register_uri_handler(server, &config_page_get);
  } else {
    ESP_LOGE(TAG, "Error starting server!");
  }
}

void stop_web_server(void) {
  if (server) {
    httpd_stop(server);
  }
}
