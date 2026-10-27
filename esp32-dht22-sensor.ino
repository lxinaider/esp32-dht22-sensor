
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>

const char* AP_SSID = "ESP32-Logger";
const char* AP_PASS = "12345678";

#define VENT_PIN 18
#define DHT_PIN 16

DHT dht(DHT_PIN, DHT22);
LiquidCrystal_I2C lcd(0x27, 16, 2);
WebServer server(80);

const char* ARQUIVO = "/log.csv";

const unsigned long INTERVALO_LCD = 2000;
const unsigned long INTERVALO_LOG = 60000;
const size_t MARGEM_LIVRE = 4096;

unsigned long tLcd = 0;
unsigned long tLog = 0;

float temp = NAN;
float umid = NAN;

float limiteUmidade = 55.0;
float limiteTemp = 30.0;

bool cheia = false;
bool ventLigado = false;

// --------------------------------------------------
// HORA E MEMORIA
// --------------------------------------------------

String agora() {
  struct tm t;

  if (getLocalTime(&t, 0)) {
    char b[20];
    strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &t);
    return String(b);
  }

  return "sem-hora-" + String(millis() / 1000);
}

size_t livre() {
  return LittleFS.totalBytes() - LittleFS.usedBytes();
}

// --------------------------------------------------
// PAGINA HTML INCORPORADA NO FIRMWARE
// --------------------------------------------------

const char CSS[] PROGMEM = R"css(
* {
    box-sizing: border-box;
}

body {
    font-family: system-ui, sans-serif;
    background: #131313;
    color: #ffffff;
    margin: 0;
    padding: 1rem;
}

.wrap {
    max-width: 480px;
    margin: 0 auto;
}

h2 {
    margin-bottom: 1rem;
}

.card {
    background: #252525;
    border: 1px solid #1f1f1f;
    border-radius: .5rem;
    padding: 1rem;
    margin-bottom: 1rem;
}

.big {
    font-size: 2rem;
    font-weight: 600;
}

.btn {
    display: block;
    width: 100%;
    text-align: center;
    padding: .75rem;
    border: 0;
    border-radius: .4rem;
    text-decoration: none;
    color: #fff;
    background: #0d6efd;
    margin-top: .75rem;
    font-size: 1rem;
    cursor: pointer;
}

.btn.red {
    background: #dc3545;
}

.btn.green {
    background: #198754;
}

small {
    color: #dfdfdf;
}

input {
    display: block;
    width: 100%;
    padding: .7rem;
    margin-top: .5rem;
    border: 1px solid #1f1f1f;
    border-radius: .4rem;
    font-size: 1rem;
}

.status {
    margin-bottom: 1rem;
}

.status.error {
    color: #dc3545;
}

.status.ok {
    color: #198754;
}
)css";

const char JS[] PROGMEM = R"js(
const $ = id => document.getElementById(id);

async function atualizar() {
    try {
        const resposta = await fetch('/dados', {
            cache: 'no-store'
        });

        if (!resposta.ok) {
            throw new Error('Falha ao consultar o ESP32');
        }

        const d = await resposta.json();

        $('t').textContent =
            d.t === null ? '--' : Number(d.t).toFixed(1);

        $('h').textContent =
            d.h === null ? '--' : Number(d.h).toFixed(1);

        $('livre').textContent =
            d.livre +
            (d.cheia ? ' KB (memória cheia!)' : ' KB');

        $('exibeLimiteUmid').textContent =
            Number(d.limiteUmid).toFixed(1);

        $('exibeLimiteTemp').textContent =
            Number(d.limiteTemp).toFixed(1);

        // Mantem os limites sincronizados entre navegadores.
        // Nao sobrescreve um campo enquanto estiver sendo editado.
        if (document.activeElement.id !== 'limiteUmid') {
            $('limiteUmid').value = d.limiteUmid;
        }

        if (document.activeElement.id !== 'limiteTemp') {
            $('limiteTemp').value = d.limiteTemp;
        }

        if (d.ventLigado === true) {
            $('vent').textContent = 'LIGADO';
            $('vent').style.color = '#198754';
        } else {
            $('vent').textContent = 'DESLIGADO';
            $('vent').style.color = '#dc3545';
        }

        $('st').textContent = 'Conectado ao ESP32';
        $('st').className = 'status ok';

    } catch (erro) {
        $('st').textContent =
            'Sem conexão com o ESP32...';

        $('st').className = 'status error';
    }
}

setInterval(atualizar, 2000);
atualizar();

// Ajusta a hora do ESP32 uma vez por sessao do navegador.
// Necessario para manter os registros com data e hora.
if (!sessionStorage.hora) {
    fetch(
        '/hora?t=' +
        Math.floor(
            Date.now() / 1000 -
            new Date().getTimezoneOffset() * 60
        )
    ).then(() => {
        sessionStorage.hora = '1';
    }).catch(() => {});
}
)js";

const char HTML[] PROGMEM = R"html(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <meta name="theme-color" content="#131313">
    <title>Logger ESP32</title>

    <style>
        %CSS%
    </style>
</head>

<body>
    <main class="wrap">

        <div id="st" class="status" aria-live="polite"></div>

        <section class="card">
            <small>Temperatura atual</small>
            <div class="big">
                <span id="t">--</span> &deg;C
            </div>
            <small>
                Limite: <span id="exibeLimiteTemp">--</span> °C
            </small>
        </section>

        <section class="card">
            <small>Umidade atual</small>
            <div class="big">
                <span id="h">--</span> %
            </div>
            <small>
                Limite: <span id="exibeLimiteUmid">--</span> %
            </small>
        </section>

        <section class="card">
            <h3>Controle de umidade</h3>

            <form action="/limite/umid" method="POST">
                <label for="limiteUmid">
                    Limite para ligar o ventilador (%)
                </label>

                <input
                    type="number"
                    name="valor"
                    id="limiteUmid"
                    min="0"
                    max="100"
                    step="0.1"
                    required
                >

                <button class="btn" type="submit">
                    Salvar limite de umidade
                </button>
            </form>
        </section>

        <section class="card">
            <h3>Controle de temperatura</h3>

            <form action="/limite/temp" method="POST">
                <label for="limiteTemp">
                    Limite para ligar o ventilador (°C)
                </label>

                <input
                    type="number"
                    name="valor"
                    id="limiteTemp"
                    min="-40"
                    max="80"
                    step="0.1"
                    required
                >

                <button class="btn" type="submit">
                    Salvar limite de temperatura
                </button>
            </form>
        </section>

        <section class="card">
            <small>
                Memória livre: <span id="livre">--</span>
                <br>
                Ventilador:
                <strong id="vent">--</strong>
            </small>
        </section>

        <a class="btn" href="/log.csv">Baixar log.csv</a>

        <a
            class="btn red"
            href="/apagar"
            onclick="return confirm('Apagar todo o log?')"
        >Apagar log</a>

    </main>

    <script>
        %JS%
    </script>

</body>
</html>
)html";

// --------------------------------------------------
// PAGINA INICIAL
// --------------------------------------------------

void paginaInicial() {
  String pagina = FPSTR(HTML);
  pagina.replace("%CSS%", FPSTR(CSS));
  pagina.replace("%JS%", FPSTR(JS));

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", pagina);
}

// --------------------------------------------------
// DADOS ENVIADOS AO NAVEGADOR
// --------------------------------------------------

void dados() {
  String j = "{\"t\":";

  j += isnan(temp) ? String("null") : String(temp, 1);

  j += ",\"h\":";
  j += isnan(umid) ? String("null") : String(umid, 1);

  j += ",\"hora\":\"" + agora() + "\"";
  j += ",\"livre\":" + String(livre() / 1024);
  j += ",\"cheia\":" + String(cheia ? "true" : "false");

  j += ",\"limiteUmid\":" + String(limiteUmidade, 1);
  j += ",\"limiteTemp\":" + String(limiteTemp, 1);

  j += ",\"ventLigado\":";
  j += ventLigado ? "true" : "false";

  j += "}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

// --------------------------------------------------
// AJUSTE DA HORA
// --------------------------------------------------

void acertarHora() {
  if (!server.hasArg("t")) {
    server.send(400, "text/plain", "Falta parametro t");
    return;
  }

  struct timeval tv;
  tv.tv_sec = server.arg("t").toInt();
  tv.tv_usec = 0;

  settimeofday(&tv, nullptr);

  server.send(200, "text/plain", "ok");
}

// --------------------------------------------------
// SALVAR LIMITE DE UMIDADE
// --------------------------------------------------

void salvarLimiteUmid() {
  if (!server.hasArg("valor") || server.arg("valor").length() == 0) {
    server.send(400, "text/plain", "Valor ausente");
    return;
  }

  String valor = server.arg("valor");
  char* fim = nullptr;
  float novoLimite = strtof(valor.c_str(), &fim);

  if (fim == valor.c_str() || *fim != '\0' || !isfinite(novoLimite) || novoLimite < 0 || novoLimite > 100) {
    server.send(400, "text/plain",
                "Umidade deve estar entre 0 e 100");
    return;
  }

  limiteUmidade = novoLimite;

  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "Limite atualizado");
}

// --------------------------------------------------
// SALVAR LIMITE DE TEMPERATURA
// --------------------------------------------------

void salvarLimiteTemp() {
  if (!server.hasArg("valor") || server.arg("valor").length() == 0) {
    server.send(400, "text/plain", "Valor ausente");
    return;
  }

  String valor = server.arg("valor");
  char* fim = nullptr;
  float novoLimite = strtof(valor.c_str(), &fim);

  if (fim == valor.c_str() || *fim != '\0' || !isfinite(novoLimite) || novoLimite < -40 || novoLimite > 80) {
    server.send(400, "text/plain",
                "Temperatura deve estar entre -40 e 80");
    return;
  }

  limiteTemp = novoLimite;

  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "Limite atualizado");
}

// --------------------------------------------------
// BAIXAR CSV
// --------------------------------------------------

void baixarLog() {
  File f = LittleFS.open(ARQUIVO, FILE_READ);

  if (!f) {
    server.send(404, "text/plain", "Sem log ainda.");
    return;
  }

  server.sendHeader(
    "Content-Disposition",
    "attachment; filename=log.csv");

  server.streamFile(f, "text/csv");
  f.close();
}

// --------------------------------------------------
// APAGAR CSV
// --------------------------------------------------

void apagarLog() {
  LittleFS.remove(ARQUIVO);
  cheia = false;

  server.sendHeader("Location", "/");
  server.send(303);
}

// --------------------------------------------------
// GRAVAR REGISTRO
// --------------------------------------------------

void gravarRegistro() {
  if (isnan(temp) || isnan(umid))
    return;

  if (livre() < MARGEM_LIVRE) {
    cheia = true;
    return;
  }

  bool novo = !LittleFS.exists(ARQUIVO);

  File f = LittleFS.open(ARQUIVO, FILE_APPEND);
  if (!f)
    return;

  if (novo)
    f.println("data_hora,temperatura_c,umidade_pct");

  f.printf("%s,%.1f,%.1f\n",
           agora().c_str(), temp, umid);
  f.close();
}

// --------------------------------------------------
// LCD
// --------------------------------------------------

void atualizarLcd() {
  char l0[17];
  char l1[17];

  if (isnan(temp)) {
    snprintf(l0, sizeof(l0), "Temp: --.- C    ");
  } else {
    snprintf(l0, sizeof(l0), "Temp: %5.1f C   ", temp);
  }

  if (cheia) {
    snprintf(l1, sizeof(l1), "Memoria cheia!  ");
  } else if (isnan(umid)) {
    snprintf(l1, sizeof(l1), "Umid: --.- %%    ");
  } else {
    snprintf(l1, sizeof(l1), "Umid: %5.1f %%   ", umid);
  }

  lcd.setCursor(0, 0);
  lcd.print(l0);

  lcd.setCursor(0, 1);
  lcd.print(l1);
}

// --------------------------------------------------
// SETUP
// --------------------------------------------------
  
void setup() {
  Serial.begin(115200);

  pinMode(VENT_PIN, OUTPUT);
  digitalWrite(VENT_PIN, LOW);

  Wire.begin(21, 22);

  lcd.init();
  lcd.backlight();
  lcd.print("Iniciando...");

  dht.begin();

  // Mantido para preservar o log existente.
  // Se o LittleFS falhar, nao formata automaticamente.
  if (!LittleFS.begin(false)) {
    lcd.clear();
    lcd.print("Erro LittleFS");

    Serial.println("Erro ao montar LittleFS.");
    Serial.println("Verifique o sistema de arquivos.");

    while (true) {
      delay(1000);
    }
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  Serial.print("Rede: ");
  Serial.println(AP_SSID);

  Serial.print("Acesse: http://");
  Serial.println(WiFi.softAPIP());

  lcd.clear();
  lcd.print(AP_SSID);
  lcd.setCursor(0, 1);
  lcd.print(WiFi.softAPIP());

  delay(3000);

  server.on("/", HTTP_GET, paginaInicial);
  server.on("/dados", HTTP_GET, dados);
  server.on("/hora", HTTP_GET, acertarHora);
  server.on("/log.csv", HTTP_GET, baixarLog);
  server.on("/apagar", HTTP_GET, apagarLog);

  server.on("/limite/umid", HTTP_POST, salvarLimiteUmid);
  server.on("/limite/temp", HTTP_POST, salvarLimiteTemp);

  server.begin();

  Serial.println("Servidor iniciado.");
}

// --------------------------------------------------
// LOOP
// --------------------------------------------------

void loop() {
  server.handleClient();

  if (millis() - tLcd >= INTERVALO_LCD) {
    tLcd = millis();

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (!isnan(t)) {
      temp = t;
    }

    if (!isnan(h)) {
      umid = h;
    }

    // Controle por OU:
    // qualquer um dos limites pode ligar o ventilador.
    bool umidadeAlta =
      !isnan(umid) && umid >= limiteUmidade;

    bool temperaturaAlta =
      !isnan(temp) && temp >= limiteTemp;

    ventLigado = umidadeAlta || temperaturaAlta;

    digitalWrite(
      VENT_PIN,
      ventLigado ? HIGH : LOW);

    atualizarLcd();
  }

  if (millis() - tLog >= INTERVALO_LOG) {
    tLog = millis();
    gravarRegistro();
  }
}
