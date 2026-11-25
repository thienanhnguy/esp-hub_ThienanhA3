#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

// ===== Chân nối =====
#define TFT_CS  15  // D8
#define TFT_DC  2   // D4
#define TFT_RST 16  // D0
// SCL -> D5, SDA -> D7, BLK -> 3V3

const char* AP_SSID = "ESP-TFT";
const char* AP_PASS = "12345678";   // tối thiểu 8 ký tự

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
ESP8266WebServer server(80);

const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP TFT</title>
<style>
body{font-family:sans-serif;text-align:center;background:#111;color:#eee;padding:16px}
canvas{border:2px solid #4caf50;width:320px;max-width:90%;image-rendering:pixelated;touch-action:none;cursor:grab}
button,input{margin:6px;font-size:16px}
button{padding:10px 16px;background:#4caf50;border:0;color:#fff;border-radius:6px}
button.g{background:#37474f;padding:8px 12px;font-size:14px}
button:disabled{background:#555}
input[type=range]{width:260px;max-width:80%}
small{color:#999}
</style></head><body>
<h2>Gui anh len man hinh</h2>
<input type="file" id="f" accept="image/*"><br>
<canvas id="c" width="160" height="128"></canvas><br>
<small>Keo anh de doi vi tri</small><br>
<span>Zoom </span><input type="range" id="z" min="0.2" max="4" step="0.01" value="1"><br>
<button class="g" id="fill">Lap day</button>
<button class="g" id="fit">Vua khung</button>
<button class="g" id="ctr">Can giua</button><br>
<button id="b" disabled>Gui len man hinh</button>
<p id="s"></p>
<script>
const c=document.getElementById('c'),x=c.getContext('2d');
const f=document.getElementById('f'),b=document.getElementById('b'),s=document.getElementById('s');
const z=document.getElementById('z');
let img=null,zoom=1,ox=0,oy=0;
const base=()=>Math.max(160/img.width,128/img.height);

function draw(){
  x.fillStyle='#000';x.fillRect(0,0,160,128);
  if(!img)return;
  const k=base()*zoom,w=img.width*k,h=img.height*k;
  x.drawImage(img,(160-w)/2+ox,(128-h)/2+oy,w,h);
}
function setZoom(v){zoom=Math.min(4,Math.max(0.2,v));z.value=zoom;draw();}

f.onchange=()=>{
  const im=new Image();
  im.onload=()=>{img=im;zoom=1;ox=0;oy=0;z.value=1;draw();b.disabled=false;s.textContent='';};
  im.src=URL.createObjectURL(f.files[0]);
};
z.oninput=()=>{zoom=parseFloat(z.value);draw();};
document.getElementById('fill').onclick=()=>{if(img){ox=0;oy=0;setZoom(1);}};
document.getElementById('fit').onclick=()=>{
  if(img){ox=0;oy=0;setZoom(Math.min(160/img.width,128/img.height)/base());}
};
document.getElementById('ctr').onclick=()=>{ox=0;oy=0;draw();};

// Keo anh bang ngon tay / chuot
let drag=false,lx=0,ly=0;
c.onpointerdown=e=>{drag=true;lx=e.clientX;ly=e.clientY;c.setPointerCapture(e.pointerId);};
c.onpointermove=e=>{
  if(!drag||!img)return;
  const r=160/c.getBoundingClientRect().width;
  ox+=(e.clientX-lx)*r;oy+=(e.clientY-ly)*r;
  lx=e.clientX;ly=e.clientY;draw();
};
c.onpointerup=c.onpointercancel=()=>{drag=false;};
c.addEventListener('wheel',e=>{e.preventDefault();setZoom(zoom*(e.deltaY<0?1.08:0.93));},{passive:false});

const sleep=ms=>new Promise(r=>setTimeout(r,ms));
b.onclick=async()=>{
  const d=x.getImageData(0,0,160,128).data;
  b.disabled=true;
  try{
    for(let y=0;y<128;y+=4){
      let hex='';
      for(let i=y*160*4;i<(y+4)*160*4;i+=4){
        // Neu mau bi nguoc do<->xanh duong: doi d[i] va d[i+2] cho nhau
        const p=((d[i]&0xF8)<<8)|((d[i+1]&0xFC)<<3)|(d[i+2]>>3);
        hex+=p.toString(16).padStart(4,'0');
      }
      const r=await fetch('/row?y='+y,{method:'POST',headers:{'Content-Type':'text/plain'},body:hex});
      if(!r.ok) throw new Error('row '+y);
      s.textContent='Dang gui '+Math.round((y+4)/128*100)+'%';
      await sleep(5);
    }
    s.textContent='Xong!';
  }catch(e){s.textContent='Loi: '+e.message;}
  b.disabled=false;
};
</script></body></html>
)rawliteral";

static uint16_t pxBuf[128];

static inline uint8_t hexv(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return 0;
}

void handleRoot() {
  server.send_P(200, "text/html", PAGE);
}

void handleRow() {
  if (!server.hasArg("plain") || !server.hasArg("y")) {
    server.send(400, "text/plain", "bad");
    return;
  }
  int y = server.arg("y").toInt();
  String d = server.arg("plain");
  int total = d.length() / 4;
  int rows = total / 160;
  if (y < 0 || rows <= 0 || y + rows > 128) {
    server.send(400, "text/plain", "range");
    return;
  }

  tft.startWrite();
  tft.setAddrWindow(0, y, 160, rows);
  int n = 0;
  for (int i = 0; i < total; i++) {
    int k = i * 4;
    pxBuf[n++] = (hexv(d[k]) << 12) | (hexv(d[k + 1]) << 8) |
                 (hexv(d[k + 2]) << 4) | hexv(d[k + 3]);
    if (n == 128) {
      tft.writePixels(pxBuf, n);
      n = 0;
    }
  }
  if (n > 0) tft.writePixels(pxBuf, n);
  tft.endWrite();

  Serial.printf("row y=%d heap=%u\n", y, ESP.getFreeHeap());
  server.send(200, "text/plain", "OK");
}

void setup() {
  Serial.begin(115200);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.softAP(AP_SSID, AP_PASS);

  server.on("/", handleRoot);
  server.on("/row", handleRow);
  server.begin();

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(4, 10);
  tft.println("WiFi: " + String(AP_SSID));
  tft.setCursor(4, 24);
  tft.println("Pass: " + String(AP_PASS));
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(4, 38);
  tft.println("Mo: 192.168.4.1");
}

void loop() {
  server.handleClient();
}
