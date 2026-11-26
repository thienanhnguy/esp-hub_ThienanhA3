#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

// Tên WiFi phát ra
const char *ssid = "I_Love_You_<3";

const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);

DNSServer dnsServer;
WebServer server(80);

// Trang Web HTML + CSS + Hiệu ứng chữ LNQT và trái tim bay + ảnh nhúng base64
const char WEB_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>I Love You <3</title>
    <style>
        body {
            margin: 0;
            padding: 0;
            background-color: #ffebef;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            height: 100vh;
            overflow: hidden;
            font-family: 'Arial', sans-serif;
        }

        .container {
            position: relative;
            text-align: center;
            z-index: 10;
        }

        /* Ảnh Hello Kitty nhún nhảy */
        .kitty-img {
            width: 190px;
            height: auto;
            animation: bounce 2s infinite ease-in-out;
            filter: drop-shadow(0px 10px 18px rgba(214, 51, 132, 0.25));
            margin-bottom: 12px;
        }

        @keyframes bounce {
            0%, 100% { transform: translateY(0) scale(1); }
            50% { transform: translateY(-15px) scale(1.05); }
        }

        h1 {
            color: #d63384;
            font-size: 2.3rem;
            margin: 10px 0 0 0;
            text-shadow: 2px 2px 4px rgba(0,0,0,0.08);
            animation: pulse 1.5s infinite;
        }

        @keyframes pulse {
            0% { transform: scale(1); }
            50% { transform: scale(1.06); }
            100% { transform: scale(1); }
        }

        /* Hiệu ứng chữ LNQT và trái tim bay lên xuống */
        .floating-item {
            position: absolute;
            user-select: none;
            pointer-events: none;
            animation: flyUp 4.5s linear infinite;
        }

        @keyframes flyUp {
            0% {
                transform: translateY(100vh) scale(0.6) rotate(0deg);
                opacity: 1;
            }
            100% {
                transform: translateY(-50vh) scale(1.3) rotate(15deg);
                opacity: 0;
            }
        }
    </style>
</head>
<body>

    <div class="container">
        <!-- Ảnh Hello Kitty nhúng trực tiếp (base64) -->
        <img class="kitty-img" alt="Hello Kitty" src="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAANcAAADcCAMAAADUZMREAAAAwFBMVEWUHV5tGUncm7ZUFDMmHB3MJHiqlJ7aKo2hcIhuUl3l2N6QW3HWssXw8mq6L4JfXiPt8i/i4Zk7Qjs7SEa8xcTEWYp8fYB8hoGHiWK6vcK8wr7CRHk/PkHAvGX////9+/wIAwXxmcHsBIvvmLwrARTr6uoWFhXwp8dOAS3ytc4oJyjIx8fNGHpIR0gzASSIiIh2dnapp6ivJnE3Nze3t7fVGIVVVVXQJ4bX19faCYZpaGjVB3foFYuWlpawGW7mBnuoAiaOAAAAQHRSTlP///////////////////////////////////////8A////////////////////////////////////////////4VbhigAAFqBJREFUeNrtnQl/osgSwLm8M5k3e7z7RQyCCiKngODx/b/Vq+oGRAVDI8TZ+aV2ZzabROVPVVdXVXcX3NuvKdwX1xfXF9cX1xfXF9cXF5GI/N03Zdn0fzF9+Z6cSvwLcTm6hEhzAiY5vwhXhLqaz5dENnN53v/LcBmWp2uyrOlmfDOC/qUD1WbJc9xg4HLiHMCGfw0uW5MLopnWhQ1KoCtRdI9rIi4/B7L4r8AVyxeCeouN/KegrKXI9Y77/fvp/f19zYmgMqmRxnzf+EQu4uk0z7atgxV7UqY0gf5UAmXxbrh+z2TQ45bgQJiu0LdNPbUJLf4kLqItf5pLZKdo3/GnOmrL7W3fz5KEHDgPndHKC6ZufQaXhZ8UTS/EMel1mG8HGFs8566LXLutK4LCglpvf9DlWzG75zLw+o3pVM2hVPzSCOR0Ip4vudcw2RW49smaW6KOa1DlWtIC2zFU1bDgluk178kjXAF8pDUtkbFHdbbhtttdEQstccBtYH6uZQrIZBftwcG39bvmQueHSjorTM2+UonGluH2imr3ftqGPFji8K1vjX7XQTwztq4v1dfpsEIotXjHIviu1jGXT3yGevXJmYDNLF+vsHaovNQSL7wBiB44l15W1g7ZTSq8rS03UBgbF37EtEpUebPhj9fa2u3f30/gEzFYlMqcgmNkJphTXb7ttInr4FjnLr2KagrOcOmGuwtNZbJ9XeIQk6TfTTMIgnx6onqjnt1WK94aBrXcLReMAbMEiY4xU56Lx4S4QEK12xfB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">

        <h1>I Love You ❤️</h1>
    </div>

    <!-- Script tạo hiệu ứng chữ LNQT và trái tim bay -->
    <script>
        function createFloatingElement() {
            const el = document.createElement('div');
            el.classList.add('floating-item');
            
            const types = ['LNQT', '❤️', '💖', '💗', '💓', '💕', 'LNQT'];
            const content = types[Math.floor(Math.random() * types.length)];
            
            el.innerHTML = content;
            
            if (content === 'LNQT') {
                el.style.color = '#ff3366';
                el.style.fontWeight = 'bold';
                el.style.fontSize = (Math.random() * 8 + 18) + 'px';
                el.style.textShadow = '0 2px 4px rgba(0,0,0,0.15)';
            } else {
                el.style.fontSize = (Math.random() * 15 + 18) + 'px';
            }

            el.style.left = Math.random() * 90 + 'vw';
            el.style.animationDuration = (Math.random() * 2 + 3) + 's';
            
            document.body.appendChild(el);

            setTimeout(() => {
                el.remove();
            }, 4500);
        }

        setInterval(createFloatingElement, 300);
    </script>

</body>
</html>
)rawliteral";

void handleRoot() {
  server.send(200, "text/html; charset=utf-8", WEB_PAGE);
}

void handleRedirect() {
  server.sendHeader("Location", String("http://") + apIP.toString(), true);
  server.send(302, "text/plain", "");
}

void setup() {
  Serial.begin(115200);

  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid);

  dnsServer.start(DNS_PORT, "*", apIP);

  server.on("/", handleRoot);
  server.on("/generate_204", handleRoot); 
  server.on("/fwlink", handleRoot);        
  server.onNotFound(handleRedirect);

  server.begin();
  Serial.println("ESP32-C3 Super Mini da san sang phat WiFi!");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
}
