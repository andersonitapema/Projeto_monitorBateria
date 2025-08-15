

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>


#include <DHT.h>


#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <FS.h>  // Para utilizar o sistema de arquivos SPIFFS





#include <WebSocketsServer.h>
#include <time.h> // Biblioteca para sincronização NTP
#include <IPAddress.h> // Inclua a biblioteca IPAddress


#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define FLASH_BUTTON 0  // GPIO0 é o botão FLASH no ESP8266
#define VOLTAGE_PIN A0  // Pino analógico para leitura da bateria


// Constantes para o divisor de tensão da bateria
#define VOLTAGE_DIVIDER_RATIO 2.0  // Ajuste este valor de acordo com seu divisor de tensão
#define VOLTAGE_REFERENCE 3.3      // Tensão de referência do ESP8266


// sensor de temperatura DTH11
#define DHTPIN D4 // Pino onde o DHT11 está conectado
#define DHTTYPE DHT11  // Definindo o tipo de sensor DHT11

DHT dht(DHTPIN, DHTTYPE);

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Estados do display
enum DisplayState {
    MAIN_SCREEN,
    MENU_WIFI_STATUS,
    MENU_IP_ADDRESS,
    MENU_SYSTEM_INFO
};

DisplayState currentState = MAIN_SCREEN;
unsigned long lastDisplayUpdate = 0;
unsigned long lastButtonPress = 0;
unsigned long menuTimeout = 0;
bool inMenu = false;
float lastTemperature = 0;
float lastVoltage = 0;


// Inicializa o servidor web
ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81); // Porta do WebSocket

// Variáveis para Wi-Fi, login e restart
String selectedSSID = "";
String enteredPassword = "";
bool isLoginAuthenticated = false;
bool loginFailed = false;
bool shouldRestartESP = false;  // Flag para saber se deve reiniciar
unsigned long restartTimer = 0;  // Timer para reiniciar o ESP

// Array para armazenar o histórico de mensagens (máximo de 20 mensagens)
String messageLog[20];
int logIndex = 0;

// Configurações do NTP
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -10800; // Ajuste de fuso horário para UTC-3 (Brasília)
const int   daylightOffset_sec = 3600; // Horário de verão (1 hora)

// Função para simular leitura de temperatura (substitua pela sua implementação real)
float readTemperature() {
   return dht.readTemperature();

}

// Função para ler a voltagem da bateria
float readBatteryVoltage() {
    int rawValue = analogRead(VOLTAGE_PIN);
    float voltage = (rawValue * VOLTAGE_REFERENCE * VOLTAGE_DIVIDER_RATIO) / 1024.0;
    return voltage;
}


// Funçao sensor de temperatura DTH11
void setupDTH() {
 dht.begin();

}


// Função para inicializar o display
void initDisplay() {
    Wire.begin(14, 12); // SDA no GPIO14 (D6) e SCL no GPIO12 (D5)
    
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("Falha ao inicializar SSD1306"));
        return;
    }
    
    pinMode(FLASH_BUTTON, INPUT_PULLUP);
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.display();
    
    // Tela inicial
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(10, 0);
    display.println(F("ESP8266"));
    display.setTextSize(1);
    display.setCursor(20, 30);
    display.println(F("Dashboard v2.0"));
    display.display();
    delay(2000);
}

// Função para mostrar a tela principal
void displayMainScreen() {
    display.clearDisplay();
    display.setTextSize(1);
    
    // Título
    display.setCursor(0, 0);
    display.println(F("Status do Sistema"));
    display.drawLine(0, 9, display.width(), 9, SSD1306_WHITE);
    
    // Voltagem da bateria
    display.setCursor(0, 16);
    display.print(F("Bateria: "));
    display.print(lastVoltage, 1);
    display.println(F("V"));
    
    // Barra de bateria
    int batteryPercentage = map(lastVoltage * 100, 320, 420, 0, 100);
    batteryPercentage = constrain(batteryPercentage, 0, 100);
    display.drawRect(90, 15, 30, 10, SSD1306_WHITE);
    display.fillRect(92, 17, (batteryPercentage * 26) / 100, 6, SSD1306_WHITE);
    
    // Temperatura
    display.setCursor(0, 32);
    display.print(F("Temp: "));
    display.print(lastTemperature, 1);
    display.println(F("C"));
    
    // Status WiFi
    display.setCursor(0, 48);
    if (WiFi.status() == WL_CONNECTED) {
        display.print(F("WiFi: "));
        display.println(WiFi.SSID());
    } else {
        display.println(F("WiFi: Desconectado"));
    }
    
    display.display();
}

// Função para mostrar status do WiFi
void displayWiFiStatus() {
    display.clearDisplay();
    display.setTextSize(1);
    
    display.setCursor(0, 0);
    display.println(F("Status WiFi:"));
    display.drawLine(0, 9, display.width(), 9, SSD1306_WHITE);
    
    if (WiFi.status() == WL_CONNECTED) {
        display.setCursor(0, 16);
        display.println("SSID: " + WiFi.SSID());
        
        int rssi = WiFi.RSSI();
        int signalStrength = map(rssi, -100, -50, 0, 100);
        display.setCursor(0, 32);
        display.print(F("Sinal: "));
        display.print(signalStrength);
        display.println(F("%"));
        
        // Barra de sinal
        display.drawRect(90, 30, 30, 10, SSD1306_WHITE);
        display.fillRect(92, 32, (signalStrength * 26) / 100, 6, SSD1306_WHITE);
    } else {
        display.setCursor(0, 16);
        display.println(F("Desconectado"));
    }
    
    display.display();
}

// Função para mostrar endereço IP
void displayIPAddress() {
    display.clearDisplay();
    display.setTextSize(1);
    
    display.setCursor(0, 0);
    display.println(F("Endereco IP:"));
    display.drawLine(0, 9, display.width(), 9, SSD1306_WHITE);
    
    display.setCursor(0, 16);
    if (WiFi.status() == WL_CONNECTED) {
        display.println(WiFi.localIP().toString());
    } else if (WiFi.getMode() == WIFI_AP) {
        display.println(WiFi.softAPIP().toString());
        display.setCursor(0, 32);
        display.println(F("Modo AP Ativo"));
    } else {
        display.println(F("Sem conexao"));
    }
    
    display.display();
}

// Função para mostrar informações do sistema
void displaySystemInfo() {
    display.clearDisplay();
    display.setTextSize(1);
    
    display.setCursor(0, 0);
    display.println(F("Info Sistema:"));
    display.drawLine(0, 9, display.width(), 9, SSD1306_WHITE);
    
    unsigned long uptime = millis() / 1000;
    int hours = uptime / 3600;
    int minutes = (uptime % 3600) / 60;
    
    display.setCursor(0, 16);
    display.print(F("Uptime: "));
    display.print(hours);
    display.print(F("h "));
    display.print(minutes);
    display.println(F("m"));
    
    display.setCursor(0, 32);
    display.print(F("Mem: "));
    display.print(ESP.getFreeHeap() / 1024);
    display.println(F("KB"));
    
    display.setCursor(0, 48);
    display.print(F("Flash: "));
    display.print(ESP.getFlashChipSize() / 1024);
    display.println(F("KB"));
    
    display.display();
}

// Função para gerenciar o botão e menu
void handleButton() {
    static bool lastButtonState = HIGH;
    static unsigned long buttonPressStart = 0;
    bool currentButtonState = digitalRead(FLASH_BUTTON);
    
    // Detecta mudança no estado do botão
    if (currentButtonState != lastButtonState) {
        if (currentButtonState == LOW) {
            // Botão foi pressionado
            buttonPressStart = millis();
        } else {
            // Botão foi solto
            unsigned long pressDuration = millis() - buttonPressStart;
            
            if (pressDuration >= 5000 && !inMenu) {
                // Pressão longa (5 segundos) - Ativa o menu
                inMenu = true;
                currentState = MENU_WIFI_STATUS;
                menuTimeout = millis();
            } else if (pressDuration < 1000 && inMenu) {
                // Pressão curta durante o menu - Alterna estados
                menuTimeout = millis(); // Reseta o timeout
                switch (currentState) {
                    case MENU_WIFI_STATUS:
                        currentState = MENU_IP_ADDRESS;
                        break;
                    case MENU_IP_ADDRESS:
                        currentState = MENU_SYSTEM_INFO;
                        break;
                    case MENU_SYSTEM_INFO:
                        currentState = MENU_WIFI_STATUS;
                        break;
                    default:
                        break;
                }
            }
        }
        lastButtonState = currentButtonState;
    }
    
    // Verifica timeout do menu
    if (inMenu && (millis() - menuTimeout >= 60000)) {
        inMenu = false;
        currentState = MAIN_SCREEN;
    }
}

// Função para atualizar o display
void updateDisplay() {
    // Atualiza leituras a cada segundo
    if (millis() - lastDisplayUpdate >= 1000) {
        lastTemperature = readTemperature();
        lastVoltage = readBatteryVoltage();
        lastDisplayUpdate = millis();
        
        // Atualiza o display de acordo com o estado atual
        if (!inMenu) {
            displayMainScreen();
        } else {
            switch (currentState) {
                case MENU_WIFI_STATUS:
                    displayWiFiStatus();
                    break;
                case MENU_IP_ADDRESS:
                    displayIPAddress();
                    break;
                case MENU_SYSTEM_INFO:
                    displaySystemInfo();
                    break;
                default:
                    break;
            }
        }
    }
}


// Função para obter a hora e data formatada
String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Falha ao obter o tempo";
  }
  char timeStringBuff[50]; // Buffer de 50 caracteres para armazenar a string formatada
  strftime(timeStringBuff, sizeof(timeStringBuff), "%d-%m-%Y %H:%M:%S", &timeinfo);
  return String(timeStringBuff);
}

// Função para adicionar uma mensagem ao log
void addMessageToLog(String message) {
  messageLog[logIndex] = message;
  logIndex = (logIndex + 1) % 20; // Roda o índice para armazenar no máximo 20 mensagens
}

// Função para interpretar comandos recebidos pelo WebSocket
void handleCommand(String command) {
if (command.startsWith("ip ")) { // Verifica se o comando começa com "ip "
    String newIpString = command.substring(3); // Extrai o novo IP
    IPAddress newIp;

    // Converte a string em um objeto IPAddress
    if (newIp.fromString(newIpString)) {
      // Define o novo IP
      if (WiFi.config(newIp, WiFi.gatewayIP(), WiFi.subnetMask())) {
        String message = "Novo IP configurado: " + newIpString ; // Mensagem de sucesso
        addMessageToLog(message); // Adiciona ao log
        webSocket.broadcastTXT(message); // Envia a mensagem ao console
      } else {
        String message = "Falha ao configurar o novo IP#"; // Mensagem de falha
      //  addMessageToLog(message); // Adiciona ao log
        webSocket.broadcastTXT(message); // Envia a mensagem ao console
      }
    } else {
      String message = "Formato de IP inválido#"; // Mensagem de formato inválido
     // addMessageToLog(message); // Adiciona ao log
      webSocket.broadcastTXT(message); // Envia a mensagem ao console
    }
    return; // Retorna para evitar processar outros comandos
  }

  // Resto do código...
  if (command == "ajuda") {
    String message = "Instruções:\n";
    message += "1. Para ligar o LED, digite: liga led\n";
    message += "2. Para desligar o LED, digite: desliga led\n";
    message += "3. Para ver o status do LED, digite: led#\n";
    message += "4. Para ver o IP e informações de Wi-Fi, digite: wifi\n";
    message += "5. Para reiniciar o ESP8266, digite: reinicia\n";
    message += "6. Para mudar o IP, digite: ip <novo_ip>\n"; // Adiciona instrução para mudar o IP
    message += "7. Pressione Enter para enviar o comando.\n";
    message += "8. Use o botão Flash para registrar a ação no log.";
    //addMessageToLog(message); // Adiciona a mensagem de ajuda ao log
    webSocket.broadcastTXT(message); // Envia a mensagem ao console
  } 

   // Comando para buscar redes Wi-Fi
  if (command == "busca wifi") {
    int numNetworks = WiFi.scanNetworks(); // Faz a varredura de redes Wi-Fi
    String message = "Redes encontradas:\n";

    for (int i = 0; i < numNetworks; i++) {
      String ssid = WiFi.SSID(i); // Obtém o SSID da rede
      int rssi = WiFi.RSSI(i); // Obtém a potência do sinal em dBm
      // Converte dBm para porcentagem
      int percentage = map(rssi, -100, -30, 0, 100);
      percentage = constrain(percentage, 0, 100); // Garante que o valor esteja entre 0 e 100

      // Adiciona ao log e à mensagem
      message += String(i + 1) + ". " + ssid + " (Sinal: " + String(percentage) + "%)\n"; 
      addMessageToLog(ssid + " (Sinal: " + String(percentage) + "%)"); // Adiciona ao log
    }

    // Envia a mensagem ao console
    webSocket.broadcastTXT(message); 
    return; // Retorna para evitar processar outros comandos
  }

   else if (command == "wifi") {
    String ssid = WiFi.SSID(); // Obtém o nome da rede Wi-Fi
    String ip = WiFi.localIP().toString(); // Obtém o endereço IP
    int rssi = WiFi.RSSI(); // Obtém a potência do sinal Wi-Fi
    int signalPercentage = map(rssi, -100, -50, 0, 100); // Converte dBm para porcentagem
    signalPercentage = constrain(signalPercentage, 0, 100); // Garante que esteja entre 0 e 100
    String message = "Rede: " + ssid + ", IP: " + ip + ", Potência: " + String(signalPercentage) + "%";
   // addMessageToLog(message); // Adiciona ao log
    webSocket.broadcastTXT(message); // Envia a mensagem ao console
  } 
 else if (command == "reinicia") {
    String message = "Reiniciando o ESP8266..."; // Mensagem de reinício
    addMessageToLog(message); // Adiciona ao log
    webSocket.broadcastTXT(message); // Envia a mensagem ao console
    delay(1000); // Espera um segundo para a mensagem ser enviada antes de reiniciar
    ESP.restart(); // Reinicia o ESP8266
  }

  
  
  
   else if (command == "getLog#") {
    // Envia o histórico de mensagens ao console
    for (int i = 0; i < 20; i++) {
      if (messageLog[i] != "") {
        webSocket.sendTXT(0, messageLog[i]); // Envia para o cliente conectado
      }
    }
  } else {
    // Comando inválido
    String invalidCommandMessage = "Comando inválido";
    //addMessageToLog(invalidCommandMessage); // Adiciona ao log
    webSocket.broadcastTXT(invalidCommandMessage); // Envia a mensagem ao console
  }
}



// Callback de WebSocket para lidar com mensagens recebidas
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_TEXT) {
    String command = String((char *)payload);
    handleCommand(command);
  }
}

// função para formatar logs de maneira mais clara
void logSystemEvent(const String& event, const String& details = "") {
    String timestamp = getFormattedTime();
    String logMessage = timestamp + " - " + event;
    if (details.length() > 0) {
        logMessage += ": " + details;
    }
    addMessageToLog(logMessage);
    webSocket.broadcastTXT(logMessage);
}




// Função para carregar as credenciais Wi-Fi
bool loadWiFiCredentials(String &ssid, String &password) {
    File file = SPIFFS.open("/wifi.txt", "r");
    if (!file) {
        Serial.println("Falha ao abrir arquivo Wi-Fi");
        return false;
    }
    ssid = file.readStringUntil('\n');
    ssid.trim();
    password = file.readStringUntil('\n');
    password.trim();
    file.close();
    return true;
}

// Função para salvar as credenciais Wi-Fi
bool saveWiFiCredentials(const String &ssid, const String &password) {
    File file = SPIFFS.open("/wifi.txt", "w");
    if (!file) {
        Serial.println("Falha ao abrir arquivo Wi-Fi para escrita");
        return false;
    }
    file.println(ssid);
    file.println(password);
    file.close();
    return true;
}

// Função para apagar credenciais Wi-Fi do SPIFFS (Reset)
void resetWiFiCredentials() {
    if (SPIFFS.exists("/wifi.txt")) {
        SPIFFS.remove("/wifi.txt");
        Serial.println("Credenciais Wi-Fi apagadas");
    } else {
        Serial.println("Nenhuma credencial salva foi encontrada.");
    }
}

// Função para verificar se o usuário está autenticado
bool isAuthenticated() {
    return isLoginAuthenticated;  // Retorna se o login foi autenticado
}

// Página de Login com erro (se houver)
void handleLoginPage() {
    String loginError = loginFailed ? "<p class='error'>Senha incorreta!</p>" : "";
    String page = R"=====( 
        <!DOCTYPE html>
        <html lang="pt-BR">
        <head>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <title>Login</title>
            <link rel="stylesheet" href="/style.css">
        </head>
        <body>
            <div class="login-container">
                <div class="login-box">
                    <h2>Login</h2>
                    <form action='/login' method='POST'>
                        <input type='password' name='password' id='password' placeholder='Digite sua senha'>
                        <button type='submit'>Entrar</button>
                    </form>
                    )=====" + loginError + R"=====(
                </div>
            </div>
            <script>
                // Função para alternar a visibilidade da senha
                function togglePasswordVisibility() {
                    var passwordField = document.getElementById('password');
                    if (passwordField.type === 'password') {
                        passwordField.type = 'text';
                    } else {
                        passwordField.type = 'password';
                    }
                }
            </script>
        </body>
        </html>
    )=====";
    server.send(200, "text/html", page);
}

// Função de verificação de login
void handleLogin() {
    loginFailed = false;
    if (server.hasArg("password")) {
        String password = server.arg("password");
        const String correctPassword = "admin";  // Substitua pela sua senha desejada
        
        String timestamp = getFormattedTime(); // Obter timestamp atual
        String clientIP = server.client().remoteIP().toString(); // Obter IP do cliente
        String logMessage;
        
        if (password == correctPassword) {
            isLoginAuthenticated = true;
            logMessage = timestamp + " - Login bem-sucedido do IP: " + clientIP;
            addMessageToLog(logMessage);
            webSocket.broadcastTXT(logMessage);
            server.sendHeader("Location", "/dashboard");
            server.send(302, "text/plain", "");
        } else {
            loginFailed = true;
            logMessage = timestamp + " - Tentativa de login falhou do IP: " + clientIP + " (Senha incorreta: " + password + ")";
            addMessageToLog(logMessage);
            webSocket.broadcastTXT(logMessage);
            server.sendHeader("Location", "/");
            server.send(302, "text/plain", "");
        }
    }
}

// Converte RSSI (em dBm) em uma porcentagem de força de sinal
int rssiToPercentage(int rssi) {
    if (rssi <= -100) {
        return 0;
    } else if (rssi >= -50) {
        return 100;
    } else {
        return 2 * (rssi + 100);  // Mapeia o intervalo de -100 a -50 dBm para 0%-100%
    }
}

// Página do Dashboard com Wi-Fi Status Dinâmico (AP ou Conectado)
void handleDashboard() {
    if (!isAuthenticated()) {
        server.sendHeader("Location", "/");
        server.send(302, "text/plain", "");
        return;
    }

    String wifiStatus = "";
    if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED) {
        wifiStatus = "Conectado"; // Quando conectado a um router
    } else {
        wifiStatus = "Access Point"; // Quando em modo AP
    }

    String dashboardPage = R"=====( 
         <!DOCTYPE html>
        <html lang="pt-BR">

        
        <head>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <title>Dashboard</title>
            <link rel="stylesheet" href="/style.css">

            
        
        <script>

       function toggleConsole() {
            var consoleContainer = document.getElementById('consoleContainer');
            var button = document.getElementById('minimizeButton');
            
            if (consoleContainer.classList.contains('hidden')) {
                consoleContainer.classList.remove('hidden');
                button.innerText = 'Minimizar Console';
            } else {
                consoleContainer.classList.add('hidden');
                button.innerText = 'Maximizar Console';
            }
        }
        
        // Quando a página carregar, o console já estará minimizado
        window.onload = function() {
            document.getElementById('consoleContainer').classList.add('hidden');
            document.getElementById('minimizeButton').innerText = 'Maximizar Console';
        };


    var socket = new WebSocket('ws://' + window.location.hostname + ':81/');
    socket.onmessage = function(event) {
      document.getElementById("console").value += event.data + "\n";
      document.getElementById("console").scrollTop = document.getElementById("console").scrollHeight; // Rolagem automática
    };
    socket.onopen = function(event) {
      socket.send("getLog#"); // Solicita o histórico de mensagens quando o WebSocket conecta
    };

    function sendCommand() {
      var command = document.getElementById("commandInput").value;
      if (command) {
        socket.send(command);
        document.getElementById("commandInput").value = ''; // Limpa o campo de texto após enviar
      }
    }

    // Função para lidar com a tecla pressionada
    document.addEventListener("DOMContentLoaded", function() {
      document.getElementById("commandInput").addEventListener("keydown", function(event) {
        if (event.key === "Enter") {
          sendCommand(); // Envia o comando quando Enter é pressionado
          event.preventDefault(); // Previne a ação padrão de nova linha
        }
      });
    });

        
        </script>


        </head>
        


        <body>
            <div class="header">
                DASHBOARD 
                <button class="logout-btn small-btn" onclick="window.location.href='/logout';">Sair</button>
            </div>
            <div class="grid-container">
                <div class="grid-item" onclick="window.location.href='/wifi';">
                    <h3>Status Wi-Fi</h3>
                    <p>)=====" + wifiStatus + R"=====(</p>
                </div>
                <div class="grid-item">
                    <h3>Modo de Operação</h3>
                    <p>Desligado</p>
                </div>
              
                <div class="grid-item">
                    <h3>Nível de Bateria</h3>
                    <p>75%</p>
                </div>
              
                <div class="grid-item">
                    <h3>Temperatura</h3>
                    <p>24°C</p>
                </div>
              
             
            </div>
          <div class="container">
    <button id="minimizeButton" class="minimize-button" onclick="toggleConsole()">Maximizar Console</button>
    <div id="consoleContainer" class="hidden">
        <h1>Console ESP8266</h1>
        <textarea id="console" readonly></textarea>
        <div class="controls-container">
            <input type="text" id="commandInput" placeholder="Digite um comando" autocapitalize="off" autocomplete="off">
            <button onclick="sendCommand()">Enviar</button>
        </div>
    </div>
</div>
        </body>
        </html>
    )=====";
    server.send(200, "text/html", dashboardPage);
}

// Página de Redes Wi-Fi com RSSI convertido para percentagem e botão 'Mostrar/Ocultar Senha'
void handleScanWifi() {
    if (!isAuthenticated()) {
        server.sendHeader("Location", "/");
        server.send(302, "text/plain", "");
        return;
    }

    String wifiScanPage = R"=====( 
        <!DOCTYPE html>
        <html lang="pt-BR">
        <head>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <title>Redes Wi-Fi</title>
            <link rel="stylesheet" href="/style.css">
            <style>
                .wifi-container {
                    display: flex;
                    flex-direction: column;
                    align-items: center;
                    justify-content: center;
                    min-height: 100vh;
                    text-align: center;
                }
                .action-buttons {
                    display: flex;
                    gap: 10px;
                    margin-top: 10px;
                }
            </style>
        </head>
        <body>
            <div class="wifi-container">
                <h1>Redes Wi-Fi</h1>
                <form action='/connect-wifi' method='POST'>
    )=====";

    int n = WiFi.scanNetworks();
    if (n == 0) {
        wifiScanPage += "<p>Nenhuma rede detectada!</p>";
    } else {
        wifiScanPage += "<select name='ssid'>";
        for (int i = 0; i < n; ++i) {
            int signalStrength = rssiToPercentage(WiFi.RSSI(i));  // Conversão para porcentagem
            wifiScanPage += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + " (" + String(signalStrength) + "% de sinal)</option>";
        }
        wifiScanPage += "</select><br>";
        wifiScanPage += "<input type='password' id='password' name='password' placeholder='Digite sua senha'><br>";
        wifiScanPage += "<input type='checkbox' onclick='togglePasswordVisibility()'> Mostrar senha<br>";
    }

    wifiScanPage += R"=====( 
        <br>
        <div class="action-buttons">
            <button type='submit'>Conectar</button>
            <button type="button" class="back-btn" onclick="window.location.href='/dashboard';">Voltar</button>
            <button type="button" class="reset-btn" onclick="window.location.href='/reset-wifi';">RESET</button>
        </div>
        </form>
        <script>
            // Função para alternar a visibilidade da senha
            function togglePasswordVisibility() {
                var passwordInput = document.getElementById('password');
                if (passwordInput.type === 'password') {
                    passwordInput.type = 'text';
                } else {
                    passwordInput.type = 'password';
                }
            }
        </script>
        </div>
        </body>
        </html>
    )=====";

    server.send(200, "text/html", wifiScanPage);
}

// Função para conectar ao Wi-Fi e salvar as credenciais
void handleConnectWifi() {
    if (server.hasArg("ssid") && server.hasArg("password")) {
        selectedSSID = server.arg("ssid");
        enteredPassword = server.arg("password");

        WiFi.begin(selectedSSID.c_str(), enteredPassword.c_str());

        String connectResultPage = R"=====( 
            <!DOCTYPE html>
            <html lang="pt-BR">
            <head>
                <meta charset="UTF-8">
                <meta name="viewport" content="width=device-width, initial-scale=1.0">
                <title>Conectando...</title>
                <link rel="stylesheet" href="/style.css">
            </head>
            <body>
                <div class="wifi-container">
                    <h1>Conectando à rede )=====" + selectedSSID + R"=====(</h1>
                    <p>Um momento... Tentando conexão.</p>
        )=====";

        int tryCount = 0;
        while (WiFi.status() != WL_CONNECTED && tryCount < 20) {
            delay(500);
            tryCount++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            // Salvando as Credenciais no SPIFFS 
            // O contador será atualizado dinamicamente de 30 até 0, e então o navegador redirecionará para o endereço IP do ESP8266.
            saveWiFiCredentials(selectedSSID, enteredPassword);
            connectResultPage += "<p>IP: <a href='http://" + WiFi.localIP().toString() + "'>" + WiFi.localIP().toString() + "</a></p>";
            connectResultPage += "<p>Conectado com sucesso! Redirecionando em <span id='counter'>30</span> segundos...</p>";
            connectResultPage += "<script>";
            connectResultPage += "  let count = 30;"; // Início do contador em 30 segundos
            connectResultPage += "  const counter = document.getElementById('counter');";
            connectResultPage += "  const interval = setInterval(() => {";
            connectResultPage += "    count--;"; 
            connectResultPage += "    counter.textContent = count;"; // Atualiza o número na página
            connectResultPage += "    if (count <= 0) {";
            connectResultPage += "      clearInterval(interval);"; // Para o contador
            connectResultPage += "      window.location.href = 'http://" + WiFi.localIP().toString() + "';"; // Redireciona para o IP
            connectResultPage += "    }";
            connectResultPage += "  }, 1000);"; // Intervalo de 1 segundo
            connectResultPage += "</script>";

            
            
            server.send(200, "text/html", connectResultPage);

            // Inicia o temporizador para reiniciar
            shouldRestartESP = true;
            restartTimer = millis();  // Marca o inicio do temporizador
        } else {
            connectResultPage += "<p>Conexão falhou! Tente novamente.</p>";
            connectResultPage += "<br><button class='back-btn small-btn' onclick=\"window.location.href='/dashboard';\">Voltar</button>";
            server.send(200, "text/html", connectResultPage);
        }
    } else {
        server.send(400, "text/html", "<h1>ERRO: SSID ou senha ausentes!</h1>");
    }
}

// Função para apagar dados Wi-Fi e reiniciar em modo AP
void handleResetWiFi() {
    if (!isAuthenticated()) {
        server.sendHeader("Location", "/");
        server.send(302, "text/plain", "");
        return;
    }
    
    resetWiFiCredentials();
    server.send(200, "text/html", "<h1>Credenciais Wi-Fi apagadas!</h1><br><button onclick=\"window.location.href='/';\">Voltar ao Início</button>");
    ESP.restart();  // Reinicia o ESP depois de limpar as credenciais
}

// Função de logout
void handleLogout() {
    if (isLoginAuthenticated) {
        String clientIP = server.client().remoteIP().toString();
        logSystemEvent("Logout realizado", "IP: " + clientIP);
    }
    isLoginAuthenticated = false;
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
}

// Função que manipula o CSS no servidor
void handleStyle() {
    String css = R"=====( 
         body {
            font-family: Arial, sans-serif;
            background-color: #1e1e1e;
            color: #ddd;
            margin: 0;
            padding: 0;
        }
        /* Header otimizado */
        .header {
            background-color: #333;
            color: #27ae60;
            padding: 15px;
            text-align: center;
            font-size: 20px;
            position: relative;
          
        }
        /* Botão "Sair" no canto direito, pequeno */
        .logout-btn {
            position: absolute;
            top: 50%;
            right: 10px;
            transform: translateY(-50%);
            padding: 6px 12px;
            background-color: #27ae60;
            border: none;
            color: white;
            cursor: pointer;
            font-size: 14px;
            transition: 0.3s;
        }
        .logout-btn:hover {
            background-color: #219150;
        }
        /* Botões de Ação */
        .back-btn, .reset-btn {
            padding: 8px 12px;
            background-color: #27ae60;
            border: none;
            color: white;
            cursor: pointer;
            transition: 0.3s;
        }
        .back-btn.small-btn {
            font-size: 14px;
            padding: 6px 10px;
        }
        .back-btn:hover, .reset-btn:hover {
            background-color: #219150;
        }
        /* Centralização de login */
        .login-container {
            display: flex;
            justify-content: center;
            align-items: center;
            height: 100vh;
        }
        .login-box {
            background-color: #2c2c2c;
            padding: 30px;
            border-radius: 10px;
            text-align: center;
            max-width: 400px;
            width: 100%;
            box-shadow: 0 0 12px rgba(0, 0, 0, 0.6);
        }
        /* Inputs e botões */
        input, button {
            padding: 10px;
            margin: 8px 0;
            width: auto;
            min-width: 120px;
            border-radius: 5px;
            border: none;
            font-size: 16px;
        }
        button {
            background-color: #27ae60;
            color: white;
            cursor: pointer;
            transition: 0.2s;
            margin-left: 20px; /* Adicionei uma margem à esquerda */
        }
        button:hover {
            background-color: #219150;
        }
        /* Mensagem de erro */
        .error {
            color: red;
            margin-top: 10px;
            font-size: 14px;
        }
        /* Centralização de grade no dashboard */
        .grid-container {
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(250px, 1fr));
            gap: 20px;
            padding: 20px;
            max-width: 1200px;
            margin: auto;
        }
        .grid-item {
            background-color: #2c2c2c;
            border-radius: 12px;
            padding: 20px;
            text-align: center;
            transition: background-color 0.2s, transform 0.2s;
        }
        .grid-item:hover {
            background-color: #3b3b3b;
        }
        h3 {
            font-size: 18px;
            color: #27ae60;
        }



/* ... (estilos Console ) ... */

.container {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    padding: 20px;
    max-width: 800px;
    margin: 0 auto;
}

#console {
    width: 100%;
    height: 300px;
    border: 1px solid #444;
    border-radius: 8px;
    padding: 15px;
    font-family: monospace;
    background-color: #2c2c2c;
    color: #ddd;
    resize: none;
    margin-bottom: 20px;
    overflow-y: scroll;
    box-sizing: border-box;
}

.controls-container {
    display: flex;
    width: 100%;
    gap: 10px;
    margin-top: 15px;
}

#commandInput {
    flex: 1;
    padding: 12px;
    border: 1px solid #444;
    border-radius: 8px;
    background-color: #2c2c2c;
    color: #ddd;
    font-size: 14px;
}

.minimize-button {
    width: auto;
    min-width: 150px;
    margin-bottom: 20px;
}

button[onclick="sendCommand()"] {
    padding: 12px 24px;
    margin: 0;
    width: auto;
    min-width: 100px;
}

#consoleContainer {
    width: 100%;
    background-color: #1e1e1e;
    border-radius: 12px;
    padding: 20px;
    box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
}

#consoleContainer h1 {
    color: #27ae60;
    text-align: center;
    margin-bottom: 20px;
    font-size: 24px;
}
    
input[type="text"] {
            width: calc(100% - 110px); /* Mantém alinhado com o botão */
    padding: 10px;
    border: 1px solid #ddd;
    border-radius: 5px;
    margin-right: 10px;
    font-size: 16px;
    box-sizing: border-box; /* Garante o mesmo comportamento */
}

.minimize-button {
            background-color: #27ae60;
            color: white;
            padding: 10px 20px;
            font-size: 16px;
            border: none;
            border-radius: 8px;
            cursor: pointer;
            transition: background-color 0.2s;
            margin-bottom: 10px;
        }

        .minimize-button:hover {
            background-color: #219150;
        }

        .hidden {
            display: none;
        }



        /* Ajustes para telas menores */
        @media (max-width: 600px) {
            .grid-container {
                grid-template-columns: 1fr;
            }
            .login-box {
                max-width: 360px;
            }
            input, button {
                min-width: auto;
            }
        }
    )=====";
    server.send(200, "text/css", css);
}

// Função para conectar à rede Wi-Fi salva ou iniciar no modo AP
void connectToWiFiOrAPMode() {
    String loadedSSID, loadedPassword;
    if (loadWiFiCredentials(loadedSSID, loadedPassword)) {
        // Tenta conectar à rede salva
        WiFi.begin(loadedSSID.c_str(), loadedPassword.c_str());

        Serial.println("Tentando conectar à última rede Wi-Fi conhecida...");

        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nConectado com sucesso: " + WiFi.SSID());
            Serial.print("Endereço IP: ");
            Serial.println(WiFi.localIP());
            return;
        }

        Serial.println("\nConexão falhou. Iniciando modo AP.");
    } else {
        Serial.println("Nenhuma credencial Wi-Fi encontrada.");
    }

    // Se falhar ou não tiver rede salva, inicia o modo AP
    WiFi.softAP("ESP8266-Dashboard");
    Serial.println("Modo AP iniciado. SSID: ESP8266-Dashboard");
    Serial.print("IP do Ponto de Acesso: ");
    Serial.println(WiFi.softAPIP());
}

// Configuração
void setup() {
    Serial.begin(115200);

     // Inicializa o display OLED
    initDisplay();

    // Inicializa o SPIFFS
    if (!SPIFFS.begin()) {
        Serial.println("Falha ao montar o sistema de arquivos; reiniciando...");
        ESP.restart();
    }

    // Tenta conectar à rede Wi-Fi salva ou inicia no modo AP
    connectToWiFiOrAPMode();
    // Configura a sincronização com o servidor NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

 // Log de inicialização do sistema
    logSystemEvent("Sistema iniciado", "ESP8266 Dashboard v2.0");
    
    if (WiFi.status() == WL_CONNECTED) {
        logSystemEvent("Conexão Wi-Fi estabelecida", "SSID: " + WiFi.SSID() + ", IP: " + WiFi.localIP().toString());
    } else {
        logSystemEvent("Modo AP ativado", "SSID: ESP8266-Dashboard, IP: " + WiFi.softAPIP().toString());
    }


    // Rotas para lidar com o login e configurações de Wi-Fi
    server.on("/", handleLoginPage);
    server.on("/login", handleLogin);
    server.on("/dashboard", handleDashboard);
    server.on("/wifi", handleScanWifi);
    server.on("/connect-wifi", handleConnectWifi);
    server.on("/logout", handleLogout);
    server.on("/reset-wifi", handleResetWiFi);  // Botão para resetar credenciais salvas
    server.on("/style.css", handleStyle);  // Servir o CSS

    // Inicia o servidor HTTP
    server.begin();
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    Serial.println("Servidor HTTP iniciado.");
}

// Loop principal do servidor
void loop() {
    server.handleClient();
    webSocket.loop();

    
     handleButton();     // Gerencia o botão e menu
    updateDisplay();    // Atualiza o display OLED

    // Verifica se deve reiniciar o ESP após 10 segundos
    if (shouldRestartESP && (millis() - restartTimer >= 10000)) {
        ESP.restart();
    }
}