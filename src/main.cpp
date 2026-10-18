#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "dashboard_embedded.h"

struct Config {
  String ssid, wifiPass, printerIP, printerSerial, printerCode, cameraURL, printerName;
} cfg;
Preferences prefs;
WebServer web(80);
WiFiClientSecure tls;
PubSubClient mqtt(tls);
String reportTopic, requestTopic;
bool portalMode = false;
uint32_t lastAttempt = 0, lastPush = 0, lastStatus = 0, messages = 0, errors = 0;
const uint32_t MQTT_BUFFER = 16384;
struct Spool { String color, type, name; int remain=-1; };
struct State {
  String gcode, status, activeTray;
  float nozzle=0, nozzleTarget=0, bed=0, bedTarget=0;
  int progress=-1, remaining=-1, layer=-1, layers=-1;
  Spool slots[16];
} state;

void loadConfig() {
  prefs.begin("bambu", true);
  cfg.ssid = prefs.getString("ssid", "");
  cfg.wifiPass = prefs.getString("wpass", "");
  cfg.printerIP = prefs.getString("host", "");
  cfg.printerSerial = prefs.getString("serial", "");
  cfg.printerCode = prefs.getString("code", "");
  cfg.cameraURL = prefs.getString("camera", "");
  cfg.printerName = prefs.getString("name", "Bambu P1S");
  prefs.end();
}
String esc(const String& s) {
  String o; o.reserve(s.length()+12);
  for (char c : s) { if(c=='&')o+="&amp;"; else if(c=='<')o+="&lt;"; else if(c=='>')o+="&gt;"; else if(c=='\"')o+="&quot;"; else if(c=='\'')o+="&#39;"; else o+=c; }
  return o;
}
String configPage() {
  String h = F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>Bambu Setup</title><style>body{font:16px system-ui;background:#0b111a;color:#eef3fa;margin:0;padding:20px}main{max-width:520px;margin:auto}label{display:block;margin:16px 0 5px}input{width:100%;box-sizing:border-box;padding:12px;background:#1a2635;border:1px solid #42536a;border-radius:9px;color:white;font-size:16px}button{background:#37bd96;color:#061510;border:0;border-radius:10px;padding:13px 20px;font-weight:700;margin-top:20px}.muted{color:#a6b6c9;font-size:13px}</style></head><body><main><h1>Bambu Monitor setup</h1><p class='muted'>Saved passwords are never displayed. Leave a password blank to keep the existing value.</p><form method='POST' action='/save'>");
  auto field = [&](const char* label, const char* key, const String& value, bool secret=false) {
    h += "<label>"+String(label)+"</label><input name='"+key+"' type='"+(secret?"password":"text")+"' value='"+(secret?"":esc(value))+"' "+(secret?"autocomplete='new-password'":"")+">";
  };
  field("Wi-Fi network (SSID)","ssid",cfg.ssid);
  field("Wi-Fi password","wpass","",true);
  field("P1S IP address","host",cfg.printerIP);
  field("P1S serial number","serial",cfg.printerSerial);
  field("P1S LAN access code","code","",true);
  field("Printer display name","name",cfg.printerName);
  field("Thingino MJPEG URL (http://CAMERA_IP/x/video.mjpg)","camera",cfg.cameraURL);
  h += F("<button type='submit'>Save and restart</button></form><p class='muted'>Setup AP password is printed in the serial monitor and on the README. Do not expose this device to the public internet.</p></main></body></html>");
  return h;
}
void saveConfig() {
  if (!web.hasArg("ssid") || !web.hasArg("host") || !web.hasArg("serial")) {web.send(400,"text/plain","Missing fields");return;}
  String ssid=web.arg("ssid"),host=web.arg("host"),serial=web.arg("serial");
  ssid.trim();host.trim();serial.trim();
  if(ssid.isEmpty()||host.isEmpty()||serial.isEmpty()||ssid.length()>32||host.length()>128||serial.length()>64) {web.send(400,"text/plain","SSID, printer IP and serial are required");return;}
  prefs.begin("bambu", false);
  prefs.putString("ssid",ssid);
  if(web.arg("wpass").length()) prefs.putString("wpass",web.arg("wpass"));
  prefs.putString("host",host);prefs.putString("serial",serial);
  if(web.arg("code").length()) prefs.putString("code",web.arg("code"));
  prefs.putString("camera",web.arg("camera").substring(0,220));
  prefs.putString("name",web.arg("name").substring(0,48));
  prefs.end();
  web.send(200,"text/html","<h2>Saved. Restarting...</h2><p>Reconnect to your home Wi-Fi and open the ESP32's IP address.</p>");
  delay(700); ESP.restart();
}
void onMqtt(char*, uint8_t* payload, unsigned int len) {
  messages++;
  StaticJsonDocument<768> filter;
  JsonObject p=filter.createNestedObject("print");
  for(const char* k : {"gcode_file","gcode_state","mc_percent","mc_remaining_time","nozzle_temper","nozzle_target_temper","bed_temper","bed_target_temper","layer_num","total_layer_num"}) p[k]=true;
  JsonObject a=p.createNestedObject("ams");a["tray_now"]=true;
  JsonObject u=a.createNestedArray("ams").createNestedObject();u["id"]=true;
  JsonObject t=u.createNestedArray("tray").createNestedObject();
  for(const char* k : {"id","tray_color","tray_type","tray_sub_brands","remain"})t[k]=true;
  DynamicJsonDocument doc(6144);
  auto e=deserializeJson(doc,payload,len,DeserializationOption::Filter(filter));
  if(e){errors++;Serial.printf("MQTT JSON error %s, bytes %u\n",e.c_str(),len);return;}
  JsonVariantConst q=doc["print"];if(q.isNull())return;
  if(!q["gcode_file"].isNull())state.gcode=q["gcode_file"].as<String>();
  if(!q["gcode_state"].isNull())state.status=q["gcode_state"].as<String>();
  if(!q["mc_percent"].isNull())state.progress=q["mc_percent"].as<int>();
  if(!q["mc_remaining_time"].isNull())state.remaining=q["mc_remaining_time"].as<int>();
  if(!q["nozzle_temper"].isNull())state.nozzle=q["nozzle_temper"].as<float>();
  if(!q["nozzle_target_temper"].isNull())state.nozzleTarget=q["nozzle_target_temper"].as<float>();
  if(!q["bed_temper"].isNull())state.bed=q["bed_temper"].as<float>();
  if(!q["bed_target_temper"].isNull())state.bedTarget=q["bed_target_temper"].as<float>();
  if(!q["layer_num"].isNull())state.layer=q["layer_num"].as<int>();
  if(!q["total_layer_num"].isNull())state.layers=q["total_layer_num"].as<int>();
  JsonVariantConst ams=q["ams"];
  if(!ams.isNull()) {
    if(!ams["tray_now"].isNull())state.activeTray=ams["tray_now"].as<String>();
    for(JsonObjectConst unit : ams["ams"].as<JsonArrayConst>()){
      int uid=unit["id"]|-1;if(uid<0||uid>3)continue;
      for(JsonObjectConst tray : unit["tray"].as<JsonArrayConst>()){
        int sid=tray["id"]|-1;if(sid<0||sid>3)continue;
        Spool& s=state.slots[uid*4+sid];
        if(!tray["tray_color"].isNull())s.color=tray["tray_color"].as<String>();
        if(!tray["tray_type"].isNull())s.type=tray["tray_type"].as<String>();
        if(!tray["tray_sub_brands"].isNull())s.name=tray["tray_sub_brands"].as<String>();
        if(!tray["remain"].isNull())s.remain=tray["remain"].as<int>();
      }
    }
  }
  lastStatus=millis();
}
void pushAll(){if(mqtt.connected()){mqtt.publish(requestTopic.c_str(),"{\"pushing\":{\"command\":\"pushall\",\"sequence_id\":\"0\"}}");lastPush=millis();}}
void statusJSON(){
  StaticJsonDocument<4096> d;
  d["wifi"]=WiFi.status()==WL_CONNECTED;d["mqtt"]=mqtt.connected();d["fresh"]=lastStatus&&(millis()-lastStatus<90000);
  d["ageSec"]=lastStatus?int((millis()-lastStatus)/1000):-1;d["freeHeap"]=ESP.getFreeHeap();d["messages"]=messages;d["jsonErrors"]=errors;
  d["gcode"]=state.gcode;d["state"]=state.status;d["progress"]=state.progress;d["remaining"]=state.remaining;
  d["nozzle"]=state.nozzle;d["nozzleTarget"]=state.nozzleTarget;d["bed"]=state.bed;d["bedTarget"]=state.bedTarget;
  d["layer"]=state.layer;d["totalLayers"]=state.layers;d["activeTray"]=state.activeTray;
  JsonArray slots=d.createNestedArray("slots");
  for(int i=0;i<16;i++){Spool& s=state.slots[i];if(s.color.isEmpty()&&s.type.isEmpty())continue;JsonObject v=slots.createNestedObject();v["id"]=i;v["color"]=s.color;v["type"]=s.type;v["name"]=s.name;v["remaining"]=s.remain;}
  String out;serializeJson(d,out);web.sendHeader("Cache-Control","no-store");web.send(200,"application/json",out);
}
void startPortal(){
  portalMode=true;WiFi.mode(WIFI_AP);WiFi.softAP("Bambu-Monitor-Setup","BambuSetup2026");
  Serial.printf("SETUP AP: Bambu-Monitor-Setup / BambuSetup2026 / http://%s\n",WiFi.softAPIP().toString().c_str());
}
void setup(){
  Serial.begin(115200);delay(500);loadConfig();
  WiFi.setSleep(false);
  if(cfg.ssid.isEmpty()||cfg.printerIP.isEmpty()||cfg.printerSerial.isEmpty())startPortal();
  else{
    WiFi.mode(WIFI_STA);WiFi.begin(cfg.ssid.c_str(),cfg.wifiPass.c_str());
    uint32_t begin=millis();while(WiFi.status()!=WL_CONNECTED&&millis()-begin<20000){delay(250);}
    if(WiFi.status()!=WL_CONNECTED)startPortal();
    else Serial.printf("Dashboard: http://%s\n",WiFi.localIP().toString().c_str());
  }
  web.on("/",HTTP_GET,[](){if(portalMode)web.send(200,"text/html",configPage());else web.send_P(200,"text/html",DASHBOARD_HTML);});
  web.on("/setup",HTTP_GET,[](){web.send(200,"text/html",configPage());});
  web.on("/save",HTTP_POST,saveConfig);
  web.on("/api/config",HTTP_GET,[](){StaticJsonDocument<512>d;d["camera"]=cfg.cameraURL;d["printerName"]=cfg.printerName;String s;serializeJson(d,s);web.send(200,"application/json",s);});
  web.on("/api/status",HTTP_GET,statusJSON);
  web.onNotFound([](){if(portalMode)web.sendHeader("Location","http://192.168.4.1/",true),web.send(302,"text/plain","");else web.send(404,"text/plain","Not found");});
  web.begin();
  if(!portalMode){
    tls.setInsecure(); // LAN TLS encryption without certificate verification; do not expose to WAN.
    mqtt.setServer(cfg.printerIP.c_str(),8883);mqtt.setCallback(onMqtt);
    if(!mqtt.setBufferSize(MQTT_BUFFER))Serial.println("WARNING: MQTT buffer allocation failed");
    mqtt.setKeepAlive(30);mqtt.setSocketTimeout(5);
    reportTopic="device/"+cfg.printerSerial+"/report";
    requestTopic="device/"+cfg.printerSerial+"/request";
  }
}
void loop(){
  web.handleClient();if(portalMode){delay(5);return;}
  if(WiFi.status()!=WL_CONNECTED){static uint32_t t=0;if(millis()-t>10000){t=millis();WiFi.reconnect();}delay(5);return;}
  if(!mqtt.connected()){
    if(millis()-lastAttempt>6000){lastAttempt=millis();String clientId="bambu-c3-"+WiFi.macAddress();
      if(mqtt.connect(clientId.c_str(),"bblp",cfg.printerCode.c_str())){mqtt.subscribe(reportTopic.c_str());pushAll();Serial.println("MQTT connected");}
      else Serial.printf("MQTT connection failed: %d, heap %u\n",mqtt.state(),ESP.getFreeHeap());
    }
  }else{mqtt.loop();if(millis()-lastPush>300000)pushAll();}
  delay(2);
}
