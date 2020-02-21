#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include "FS.h"
#include "SD_MMC.h"
#include <time.h>
#include "esp_http_server.h"

const char* ssid = "YourSSID";
const char* password = "YourPassword";

// for fixed IP Address
IPAddress ip(192, 168, 0, 1);			//YourIP
IPAddress gateway(192, 168, 0, 1);		//YourGateway
IPAddress subnet(255, 255, 255, 0);		//YourSubnet
IPAddress DNS(192, 168, 0, 1);			//YourDNS

#define CAMERA_MODEL_AI_THINKER
#if defined(CAMERA_MODEL_AI_THINKER)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
#endif

#define JST     3600*9
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

IPAddress localIp;
bool isSDExist = 0;
httpd_handle_t httpd_stream = NULL;
httpd_handle_t httpd_camera = NULL;
camera_fb_t* fb = NULL;
bool semaphore = false;

void setupSerial(){
  Serial.begin(115200);
  Serial.setDebugOutput(true);
}

int setupSD(){
  int sdRet = SD_MMC.begin();
  Serial.printf("setupSD: SD_MMC.begin() -> %d\n", sdRet);

  uint8_t cardType = SD_MMC.cardType();
  if(cardType != CARD_NONE){
    isSDExist = 1;
  }

  switch(cardType){
    case CARD_NONE: Serial.println("setupSD: Card: None");      break;
    case CARD_MMC:  Serial.println("setupSD: Card Type: MMC");  break;
    case CARD_SD:   Serial.println("setupSD: Card Type: SDSC"); break;
    case CARD_SDHC: Serial.println("setupSD: Card Type: SDHC"); break;
    default:        Serial.println("setupSD: Card: UNKNOWN");   break;
  }
}

int setupCamera(){
  int ret = 0;
  
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err == ESP_OK) {
    Serial.println("setupCamera: OK");
    ret = 1;
  }else{
    Serial.printf("setupCamera: err: 0x%08x\n", err);
    ret = 0;    
  }

  return ret;
}

int setupWifi(){
  WiFi.config(ip, gateway, subnet, DNS);   // Set fixed IP address
  WiFi.begin(ssid, password);

  Serial.print("seupWifi: connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("seupWifi: connected");
  localIp = WiFi.localIP();
  return 1;
}

int setupLocalTime(){
  time_t t;
  struct tm *tm;
  Serial.print("setupLocalTime: start config");
  configTime(JST, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");
  do {
    delay(500);
    t = time(NULL);
    tm = localtime(&t);
    Serial.print(".");
  } while(tm->tm_year + 1900 < 2000);
  Serial.println("");
  Serial.printf("setupLocalTime: %04d/%02d/%02d - %02d:%02d:%02d\n",
    tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
  return 1;
}

static esp_err_t handle_index(httpd_req_t *req){
  static char resp_html[1024];
  char * p = resp_html;
  
  p += sprintf(p, "<!doctype html>");
  p += sprintf(p, "<html>");
  p += sprintf(p, "<head>");
  p += sprintf(p, "<meta name=\"viewport\" content=\"width=device-width\">");
  p += sprintf(p, "<title>ESP32-CAM</title>");
  p += sprintf(p, "</head>");
  p += sprintf(p, "<body>");
  p += sprintf(p, "<div align=center>");
  p += sprintf(p, "<img id=stream src=\"\">");
  p += sprintf(p, "</div>");  
  p += sprintf(p, "<div align=center>");
  p += sprintf(p, "<button id=takepic>Take Picture</button>");
  p += sprintf(p, "</div>");  
  p += sprintf(p, "<script>");
  p += sprintf(p, "document.addEventListener('DOMContentLoaded', function (event) {\n");
  p += sprintf(p, "  var baseHost = document.location.origin;\n");
  p += sprintf(p, "  var streamUrl = baseHost + ':81';\n");
  p += sprintf(p, "  const view = document.getElementById('stream');\n");
  p += sprintf(p, "  document.getElementById('stream').src = `${streamUrl}/stream`;\n");
  p += sprintf(p, "  document.getElementById('takepic').onclick = () => {\n");
  p += sprintf(p, "    window.stop();\n");
  p += sprintf(p, "    view.src = `${baseHost}/capture?_cb=${Date.now()}`;\n");
  p += sprintf(p, "    console.log(view.src);\n");
  p += sprintf(p, "    setTimeout(function(){\n");
  p += sprintf(p, "      view.src = `${streamUrl}/stream`;\n");
  p += sprintf(p, "      console.log(view.src);\n");
  p += sprintf(p, "    }, 3000);\n");
  p += sprintf(p, "  }\n");
  p += sprintf(p, "})");
  p += sprintf(p, "\n");
  p += sprintf(p, "</script>");
  p += sprintf(p, "</body>");
  p += sprintf(p, "</html>");
  *p++ = 0;

  Serial.printf("handle_index: len: %d\n", strlen(resp_html));

  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, resp_html, strlen(resp_html));
}

void takePic(camera_fb_t* fb){
  Serial.println("takePic: Start");
  if(!isSDExist){
    Serial.println("takePic: SD Card none");    
    return;
  }

  if (!fb) {
    Serial.println("takePic: image data none");
    return;
  }

  time_t t = time(NULL);
  struct tm *tm;
  tm = localtime(&t);
  
  char filename[32];
  sprintf(filename, "/img%04d%02d%02d%02d%02d%02d.jpg",
    tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
    
  fs::FS &fs = SD_MMC;
  File imgFile = fs.open(filename, FILE_WRITE);
  if(imgFile && fb->len > 0){
    imgFile.write(fb->buf, fb->len);
    imgFile.close();
    Serial.print("takePic: ");
    Serial.println(filename);
  }else{
    Serial.println("takePic: Save file failed");
    Serial.printf("takePic: imgFile: %d\n", imgFile);
    Serial.printf("takePic: fb->len: %d\n", fb->len);      
  }
}

esp_err_t resp_jpg(httpd_req_t *req, bool isSave){
  Serial.printf("resp_jpg: isSave: %d\n", isSave);
  esp_err_t res = ESP_OK;

  if(fb){
    esp_camera_fb_return(fb);
    fb = NULL;
  }
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("resp_jpg: Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  if(isSave){
    // save image to SD card
    takePic(fb);
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  Serial.printf("resp_jpg: JPG: %uB\n", (uint32_t)(fb->len));
  esp_camera_fb_return(fb);
  fb = NULL;
  Serial.println("resp_jpg: end");
  return res;  
}

static esp_err_t handle_jpg(httpd_req_t *req){
  while(semaphore){
    delay(100);
  }
  semaphore = true;
  Serial.println("handle_jpg: Start");
  esp_err_t ret = resp_jpg(req, false);
  semaphore = false;
  return ret;
}

static esp_err_t handle_capture(httpd_req_t *req){
  while(semaphore){
    delay(100);
  }
  semaphore = true;
  Serial.println("handle_capture: Start");
  esp_err_t ret = resp_jpg(req, true);
  semaphore = false;
  return ret;
}

static esp_err_t handle_stream(httpd_req_t *req){
  while(semaphore){
    delay(100);
  }
  semaphore = true;

  //camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  char * part_buf[64];
    
  Serial.println("handle_stream: Start");
  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if(res != ESP_OK){
    Serial.println("handle_stream: err: httpd_resp_set_type");
    semaphore = false;
    return res;
  }

  while(true){
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("handle_stream: Camera capture failed");
      res = ESP_FAIL;
    } else {
      Serial.printf("handle_stream: buffer: %dB\n", fb->len);
    }
    if(res == ESP_OK){
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, fb->len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if(res == ESP_OK){
      res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    }
    if(res == ESP_OK){
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if(fb){
      esp_camera_fb_return(fb);
      fb = NULL;
    }
    if(res != ESP_OK){
      break;
    }
  }

  Serial.println("handle_stream: exit");
  semaphore = false;
  return res;
}

int setupWebServer(){
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  
  httpd_uri_t uri_index = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = handle_index,
    .user_ctx  = NULL
  };

  httpd_uri_t uri_jpg = {
    .uri       = "/jpg",
    .method    = HTTP_GET,
    .handler   = handle_jpg,
    .user_ctx  = NULL
  };

  httpd_uri_t uri_capture = {
    .uri       = "/capture",
    .method    = HTTP_GET,
    .handler   = handle_capture,
    .user_ctx  = NULL
  };

  httpd_uri_t uri_stream = {
    .uri       = "/stream",
    .method    = HTTP_GET,
    .handler   = handle_stream,
    .user_ctx  = NULL
  };

  Serial.printf("setupWebServer: web port: '%d'\n", config.server_port);
  if (httpd_start(&httpd_camera, &config) == ESP_OK) {
    httpd_register_uri_handler(httpd_camera, &uri_index);
    httpd_register_uri_handler(httpd_camera, &uri_jpg);
    httpd_register_uri_handler(httpd_camera, &uri_capture);
  }  

  config.server_port += 1;
  config.ctrl_port += 1;
  Serial.printf("setupWebServer: stream port: '%d'\n", config.server_port);
  if (httpd_start(&httpd_stream, &config) == ESP_OK) {
    httpd_register_uri_handler(httpd_stream, &uri_stream);
  }
}

void setup() {
  // put your setup code here, to run once:
  setupSerial();
  if(!setupSD()){
    Serial.println("setupSD failed.");
    return;
  }
  if(!setupCamera()){
    Serial.println("setupCamera failed.");
    return;
  }
  if(!setupWifi()){
    Serial.println("setupWifi failed.");
    return;
  }
  if(!setupLocalTime()){
    Serial.println("setupLocalTime failed.");
    return;
  }
  setupWebServer();
  
  Serial.print("Camera Ready! Use 'http://");
  Serial.print(localIp);
  Serial.println("' to connect");
}

void loop() {
  // put your main code here, to run repeatedly:
  delay(10000);
}
