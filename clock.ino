// disable spotify features
#define DISABLE_ALBUM
#define DISABLE_ARTIST
#define DISABLE_AUDIOBOOKS
#define DISABLE_CATEGORIES
#define DISABLE_CHAPTERS
#define DISABLE_EPISODES
#define DISABLE_GENRES
#define DISABLE_MARKETS
#define DISABLE_PLAYLISTS
#define DISABLE_SEARCH
#define DISABLE_SHOWS
#define DISABLE_TRACKS
#define DISABLE_USER

#include "Adafruit_Debounce.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <Preferences.h>
#include <SpotifyEsp32.h>
#include <Thread.h>
#include <ThreadController.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <oled.h>

enum Mode {
    CLOCK,
    SPOTIFY,
    USAGE,
    CONFIG,
    INFO
};

enum Options {
    BRIGHTNESS,
    WEATHER_REFRESH,
    ENERGY_SAVING,
    CLOCK_STYLE,
    ABOUT
};

enum ClockStyles {
    LARGE,
    COMPACT
};

const uint8_t playSymbol[] = {
    0, 0, 255, 255, 126, 60, 24, 0
};

const uint8_t pauseSymbol[] = {
    0, 255, 255, 0, 0, 255, 255, 0
};

const uint8_t spotifySymbol[] {
    60, 126, 213, 213, 181, 237, 122, 60
};

const uint8_t pcSymbol[] {
    191, 161, 173, 237, 237, 173, 161, 191
};

const uint8_t tempSymbol[] {
    0b00000000, 0b01110000, 0b10001111, 0b10110001, 0b10110001, 0b10001111, 0b01110000, 0b00000000
};

const uint8_t sunriseSymbol[] {
    0b10100000, 0b10000000, 0b11010010, 0b11101111, 0b11101111, 0b11010010, 0b10000000, 0b10100000
};

const uint8_t sunsetSymbol[] {
    0b10100000, 0b10000000, 0b11010100, 0b11101111, 0b11101111, 0b11010100, 0b10000000, 0b10100000
};

const uint8_t optionsIcon[] {
    0b00100100, 0b01110100, 0b00100100, 0b00100100, 0b00100100, 0b00100100, 0b00101110, 0b00100100
};

const uint8_t refreshIcon[] {
    0b00111100, 0b01000010, 0b10000001, 0b10000001, 0b10000001, 0b1000101, 0b01000110, 0b00110111
};

const uint8_t crashIcon[]
{
    255, 0b00000111, 0b01010100, 0b00100111, 0b01010111, 0b00000100, 0b00000100, 0b00000100, 0b00000100, 0b00000100, 0b00000100, 0b01010100, 0b00100100, 0b01010100, 0b00000111, 255,
    255, 0b10000000, 0b10000000, 0b10000000, 0b10010000, 0b10001001, 0b10001010, 0b10001010, 0b10001001, 0b10001000, 0b10010000, 0b10010000, 0b10100000, 0b10000000, 0b10000000, 255
};

// VERY IMPORTANT AND VERY SECRET VARIABLES, DO NOT FORGET TO REMOVE BEFORE PUBLISHING THIS
const char* SSID = "";
const char* PASSWORD = "";
const char* CLIENT_ID = "";
const char* CLIENT_SECRET = "";
const char* CLIENT_REFRESH = "";
const String SERVER = ""; // http server here which returns "<cpu usage 0 to 1>;<ram usage 0 to 1>" on GET request
const String WEATHER_URL = "https://api.open-meteo.com/";

const int DSP_CLK = 37;
const int DSP_DA = 39;
const int MODE_BUTTON_OUT = 3;
const int MODE_BUTTON_IN = 2;
const int LEFT_BUTTON_OUT = 6;
const int LEFT_BUTTON_IN = 7;
const int RIGHT_BUTTON_OUT = 4;
const int RIGHT_BUTTON_IN = 5;
const int SPEAKER = 34;

enum Mode currentMode = CLOCK;
String version = String("1.1");

char brightness = 2;
bool refreshWeather = true;
bool energySaving = false;
enum ClockStyles style = LARGE;

bool locked = true;
unsigned long timeSinceInClock;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7200);
HTTPClient http;
HTTPClient weather;
Spotify sp(CLIENT_ID, CLIENT_SECRET, CLIENT_REFRESH, true);
OLED Display(DSP_DA, DSP_CLK, NO_RESET_PIN, OLED::W_128, OLED::H_64, OLED::CTRL_SH1106, 0x3C);
Adafruit_Debounce left(LEFT_BUTTON_IN, HIGH);
Adafruit_Debounce right(RIGHT_BUTTON_IN, HIGH);
Adafruit_Debounce mode(MODE_BUTTON_IN, HIGH);
Preferences preferences;

ThreadController controller = ThreadController();
Thread inputThread = Thread();
Thread spotifyThread = Thread();
Thread clockThread = Thread();
Thread usageThread = Thread();
Thread weatherThread = Thread();
Thread configThread = Thread();

String formattedDate;
String dayStamp;
String timeStamp;
bool refreshingData = false;

String lastArtist;
String lastTrack;
bool lastPlaying;
unsigned int lastProgress;
unsigned int lastDuration;

float lastCPU;
float lastRAM;
bool computerOnline;

JsonDocument lastWeather;

enum Options currentOption = BRIGHTNESS;

void lock(bool shouldLock)
{
    if (!shouldLock)
    {
        locked = false;
        preferences.begin("clock", false);
        switch (preferences.getChar("brightness", 2)) {
        case 2:
            Display.set_contrast(128);
            break;
        case 1:
            Display.set_contrast(64);
            break;
        case 0:
            Display.set_contrast(32);
            break;
        }
        brightness = preferences.getChar("brightness", 2);
        preferences.end();
        weatherThread.enabled = true;
        timeSinceInClock = millis();
        clockThread.setInterval(100);
        getWeather();
    } else
    {
        locked = true;
        weatherThread.enabled = false;
        Display.set_contrast(10);
    }
}

void checkInput()
{
    left.update();
    right.update();
    mode.update();
    if (mode.justPressed()) {
        tone(SPEAKER, 1193, 100);
        if (locked)
        {
            lock(false);
        } else {
            if (currentMode == CLOCK) {
                currentMode = USAGE;
                clockThread.enabled = false;
                usageThread.enabled = true;
            } else if (currentMode == SPOTIFY) {
                currentMode = USAGE;
                spotifyThread.enabled = false;
                usageThread.enabled = true;
            } else if (currentMode == USAGE) {
                currentMode = CONFIG;
                usageThread.enabled = false;
                configThread.enabled = true;
            } else if (currentMode == CONFIG) {
                currentMode = CLOCK;
                configThread.enabled = false;
                clockThread.enabled = true;
                timeSinceInClock = millis();
            }
        }
    }
    if (currentMode == SPOTIFY) {
        if (left.justPressed()) {
            tone(SPEAKER, 1193, 100);
            if (sp.is_playing()) {
                sp.pause_playback();
            } else {
                sp.start_resume_playback();
            }
        }

        if (right.justPressed()) {
            tone(SPEAKER, 1193, 100);
            sp.skip();
        }
    } else if (currentMode == CLOCK) {
        if (!locked)
        {
            if (left.justPressed()) {
                tone(SPEAKER, 1193, 100);
                lock(true);
            }
            if (right.justPressed()) {
                tone(SPEAKER, 1193, 100);
                Display.draw_rectangle(120, 0, 128, 8, OLED::SOLID, OLED::BLACK);
                Display.draw_bitmap(120, 0, 8, 8, refreshIcon);
                Display.display();
                digitalWrite(LED_BUILTIN, HIGH);
                timeClient.forceUpdate();
                getWeather();
                digitalWrite(LED_BUILTIN, LOW);
                Display.draw_rectangle(120, 0, 128, 8, OLED::SOLID, OLED::BLACK);
                Display.display();
            }
        }
    } else if (currentMode == CONFIG) {
        if (left.justPressed()) {
            tone(SPEAKER, 1193, 100);
            if (currentOption == BRIGHTNESS) {
                currentOption = WEATHER_REFRESH;
            } else if (currentOption == WEATHER_REFRESH) {
                currentOption = ENERGY_SAVING;
            }  else if (currentOption == ENERGY_SAVING) {
                currentOption = CLOCK_STYLE;
            } else if (currentOption == CLOCK_STYLE)
            {
                currentOption = ABOUT;
            } else if (currentOption == ABOUT)
            {
                currentOption = BRIGHTNESS;
            }
        }

        if (right.justPressed()) {
            tone(SPEAKER, 1193, 100);
            preferences.begin("clock", false);
            if (currentOption == BRIGHTNESS) {
                brightness++;
                if (brightness == 3)
                    brightness = 0;

                switch (brightness) {
                case 2:
                    Display.set_contrast(128);
                    break;
                case 1:
                    Display.set_contrast(64);
                    break;
                case 0:
                    Display.set_contrast(32);
                    break;
                }
                preferences.putChar("brightness", brightness);
            } else if (currentOption == WEATHER_REFRESH) {
                if (refreshWeather == true) {
                    refreshWeather = false;
                } else {
                    refreshWeather = true;
                }
                preferences.putBool("weather", refreshWeather);
                getWeather();
            } else if (currentOption == ENERGY_SAVING) {
                if (energySaving == true) {
                    energySaving = false;
                } else {
                    energySaving = true;
                }
                preferences.putBool("energy_saving", energySaving);
            } else if (currentOption == CLOCK_STYLE)
            {
                if (style == LARGE) {
                    style = COMPACT;
                    preferences.putChar("style", 1);
                } else if (style == COMPACT) {
                    style = LARGE;
                    preferences.putChar("style", 0);
                }
                    preferences.end();
            } else if (currentOption == ABOUT)
            {
                currentMode = INFO;
            }
            preferences.end();
        }
    } else if (currentMode == INFO) {
        if (left.justPressed()) {
            tone(SPEAKER, 1193, 100);
            currentMode = CONFIG;
        }
    }
}

void getWeather()
{
    /* so i don't spam service*/
    if (!refreshWeather) {
        char json[] = "{'current_units': {'temperature_2m': '°C'}, 'current': {'temperature_2m': 0, 'weather_code':255},'daily': {'sunrise': ['2024-05-03T--:--'],'sunset': ['2024-05-03T--:--']}}";
        deserializeJson(lastWeather, json);
    } else {
        weather.begin(WEATHER_URL.c_str());
        int httpResponseCode = weather.GET();

        if (httpResponseCode > 0) {
            Serial.print("HTTP Response code: ");
            Serial.println(httpResponseCode);
            deserializeJson(lastWeather, weather.getString());
        } else {
            Serial.print("Error code: ");
            Serial.println(httpResponseCode);
        }
        weather.end();
    }
}

void runSpotify()
{
    if (currentMode == SPOTIFY) {
        Display.clear();
        Display.draw_bitmap(0, 0, 8, 8, spotifySymbol);
        Display.draw_string(10, 0, "Spotify");
        drawWiFiBars();
        Display.draw_rectangle(0, 9, 128, 9);
        Display.setCursor(0, 11);
        Display.print((insert_newlines(lastArtist, 20).length() > 35 ? insert_newlines(lastArtist, 20).substring(0, 35) + "..." : insert_newlines(lastArtist, 20)) + "\n");
        Display.print((insert_newlines(lastTrack, 20).length() > 35 ? insert_newlines(lastTrack, 20).substring(0, 36) + "..." : insert_newlines(lastTrack, 20)));
        if (lastPlaying) {
            int secondsProgress = lastProgress / 1000;
            int minutesProgress = secondsProgress / 60;
            secondsProgress %= 60;

            int secondsDuration = lastDuration / 1000;
            int minutesDuration = secondsDuration / 60;
            secondsDuration %= 60;

            String timeProgress = String(minutesProgress, DEC) + ":" + (String(secondsProgress, DEC).length() == 1 ? "0" : "") + String(secondsProgress, DEC) + "/" + String(minutesDuration, DEC) + ":" + (String(secondsDuration, DEC).length() == 1 ? "0" : "") + String(secondsDuration, DEC);

            Display.draw_bitmap(2, 53, 8, 8, playSymbol);
            Display.draw_string(12, 54, timeProgress.c_str());
            Display.draw_rectangle(String(minutesDuration, DEC).length() == 1 ? 68 : 79, 54, 127, 60);
            Display.draw_rectangle(String(minutesDuration, DEC).length() == 1 ? 68 : 79, 54, lerp(String(minutesDuration, DEC).length() == 1 ? 68 : 79, 127, (float)lastProgress / (float)lastDuration), 60, OLED::SOLID);
        } else {
            Display.draw_bitmap(2, 53, 8, 8, pauseSymbol);
        }
        Display.display();

        JsonDocument doc = sp.currently_playing().reply;
        serializeJson(doc, Serial);
        String currentArtist = sp.current_artist_names();
        String currentTrackname = sp.current_track_name();
        Serial.print(currentArtist);
        Serial.print(currentTrackname);

        if (currentArtist == "Something went wrong" || currentArtist.isEmpty()) {
            currentArtist = " -";
        }

        if (currentTrackname == "Something went wrong" || currentTrackname == "null") {
            currentTrackname = " -";
        }
        lastTrack = currentTrackname;
        lastArtist = currentArtist;
        lastPlaying = sp.is_playing();
        lastProgress = doc["progress_ms"];
        lastDuration = doc["item"]["duration_ms"];
    }
}

void runClock()
{
    if (currentMode == CLOCK) {
        while (!timeClient.update()) {
            timeClient.forceUpdate();
        }
        Display.clear();

        formattedDate = timeClient.getFormattedDate();
        Serial.println(formattedDate);

        int splitT = formattedDate.indexOf("T");
        dayStamp = formattedDate.substring(0, splitT);
        String year = dayStamp.substring(0, 4);
        String month = dayStamp.substring(5, 7);
        String day = dayStamp.substring(8);
        dayStamp = day + "." + month + "." + year;
        timeStamp = formattedDate.substring(splitT + 1, formattedDate.length() - 1);
        int hours = timeStamp.substring(0, 2).toInt();
        int minutes = timeStamp.substring(3, 5).toInt();
        int seconds = timeStamp.substring(6).toInt();
        int yearInt = year.toInt();
        int monthInt = month.toInt();
        int dayInt = day.toInt();

        // weather
        const char* sunriseChar = lastWeather["daily"]["sunrise"][0];
        const char* sunsetChar = lastWeather["daily"]["sunset"][0];
        float temperatureChar = lastWeather["current"]["temperature_2m"];
        const char* temp_units = lastWeather["current_units"]["temperature_2m"];
        int code = lastWeather["current"]["weather_code"];

        String sunrise = String(sunriseChar);
        String sunset = String(sunsetChar);
        String temperature = String((int)temperatureChar);
        temperature.concat(String(temp_units));
        sunrise = sunrise.substring(sunrise.indexOf("T") + 1, sunrise.length());
        sunset = sunset.substring(sunset.indexOf("T") + 1, sunset.length());
        String forecast;
        switch (code) {
            case 0:
                forecast += "Clear sky";
                break;
            case 1:
                forecast += "Mainly clear";
                break;
            case 2:
                forecast += "Partly cloudy";
                break;
            case 3:
                forecast += "Overcast";
                break;
            case 45:
                forecast += "Fog";
                break;
            case 48:
                forecast += "Deposting rime fog";
                break;
            case 51:
                forecast += "Light drizzle";
                break;
            case 53:
                forecast += "Moderate drizzle";
                break;
            case 55:
                forecast += "Dense drizzle";
                break;
            case 56:
                forecast += "Light freezing drizzle";
                break;
            case 57:
                forecast += "Dense freezing drizzle";
                break;
            case 61:
                forecast += "Light rain";
                break;
            case 63:
                forecast += "Moderate rain";
                break;
            case 65:
                forecast += "Heavy rain";
                break;
            case 66:
                forecast += "Light freezing rain";
                break;
            case 67:
                forecast += "Heavy freezing rain";
                break;
            case 71:
                forecast += "Slight snow fall";
                break;
            case 73:
                forecast += "Moderate snow fall";
                break;
            case 75:
                forecast += "Heavy snow fall";
                break;
            case 77:
                forecast += "Snow grains";
                break;
            case 80:
                forecast += "Slight rain showers";
                break;
            case 81:
                forecast += "Moderate rain showers";
                break;
            case 82:
                forecast += "Heavy rain showers";
                break;
            case 85:
                forecast += "Slight snow showers";
                break;
            case 255:
                forecast += "Weather disabled";
                break;
        }

        // now that we got everything ready, we can check for selected style and display it

        if (!locked)
        {
            if (style == LARGE) {
                Display.drawString(0, 0, timeStamp.c_str(), OLED::DOUBLE_SIZE);
                Display.drawString(0, 2, dayStamp.c_str(), OLED::DOUBLE_SIZE);
                Display.draw_bitmap(2, 40, 8, 8, tempSymbol);
                Display.draw_string(12, 41, temperature.c_str());
                Display.draw_bitmap(2, 50, 8, 8, sunriseSymbol);
                Display.draw_string(12, 51, sunrise.c_str());
                Display.draw_bitmap(50, 50, 8, 8, sunsetSymbol);
                Display.draw_string(60, 51, sunset.c_str());
                Display.drawString(0, 4, forecast.c_str());
            } else if (style == COMPACT) {
                Display.drawString(6, 3, timeStamp.c_str());
                Display.drawString(5, 4, dayStamp.c_str());
            }
        } else {
            Display.drawString(6, 1, timeStamp.substring(0, 5).c_str(), OLED::DOUBLE_SIZE);
            Display.drawString(1, 3, dayStamp.c_str(), OLED::DOUBLE_SIZE);
            Display.drawString(1, 7, "Press mode to unlock");
        }

        Display.display();

        if (locked)
        {
            clockThread.setInterval(30*1000);
        }

        if (millis() - timeSinceInClock > 10*60*1000 && !locked && energySaving)
        {
            lock(true);
        }
    }
}

void runUsage()
{
    Display.clear();
    Display.draw_bitmap(0, 0, 8, 8, pcSymbol);
    Display.draw_string(10, 0, "PC status");
    drawWiFiBars();
    Display.draw_rectangle(0, 9, 128, 9);
    Display.setCursor(0, 11);
    if (!computerOnline) {
        Display.print("Powered off");
    } else {
        Display.print(String("CPU: " + String(lastCPU * 100).substring(0, String(lastCPU * 100).lastIndexOf(".")) + "%"));
        Display.draw_rectangle(0, 19, 100, 25);
        Display.draw_rectangle(0, 19, lastCPU * 100, 25, OLED::SOLID);
        Display.setCursor(0, 27);
        Display.print(String("RAM: " + String(lastRAM * 100).substring(0, String(lastRAM * 100).lastIndexOf(".")) + "%"));
        Display.draw_rectangle(0, 36, 100, 42);
        Display.draw_rectangle(0, 36, lastRAM * 100, 42, OLED::SOLID);
    }
    Display.display();

    http.begin(SERVER.c_str());
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
        Serial.print("HTTP Response code: ");
        Serial.println(httpResponseCode);
        String payload = http.getString();
        lastCPU = payload.substring(0, payload.lastIndexOf(";")).toFloat();
        lastRAM = payload.substring(payload.lastIndexOf(";") + 1).toFloat();
        computerOnline = true;
    } else {
        Serial.print("Error code: ");
        Serial.println(httpResponseCode);
        lastCPU = 0;
        lastRAM = 0;
        computerOnline = false;
    }
    http.end();
}

void runOptions()
{
    Display.clear();
    Display.draw_bitmap(0, 0, 8, 8, optionsIcon);
    Display.draw_string(10, 0, (currentMode == CONFIG ? "Settings" : "About"));
    drawWiFiBars();
    Display.draw_rectangle(0, 9, 128, 9);
    Display.setCursor(0, 11);
    if (currentMode == CONFIG)
    {
        if (currentOption == BRIGHTNESS) {
            Display.inverse();
        }
        Display.print("Brightness: " + String(brightness + 1) + "\n");
        if (currentOption == BRIGHTNESS) {
            Display.noInverse();
        }

        if (currentOption == WEATHER_REFRESH) {
            Display.inverse();
        }
        Display.print("Weather: " + String(refreshWeather) + "\n");
        if (currentOption == WEATHER_REFRESH) {
            Display.noInverse();
        }

        if (currentOption == ENERGY_SAVING) {
            Display.inverse();
        }
        Display.print("Energy saving: " + String(energySaving) + "\n");
        if (currentOption == ENERGY_SAVING) {
            Display.noInverse();
        }

        if (currentOption == CLOCK_STYLE) {
            Display.inverse();
        }
        Display.print(String(String("Clock style: ") + String(style == LARGE ? "large" : style == COMPACT ? "compact" : "") + String("\n")).c_str());
        if (currentOption == CLOCK_STYLE) {
            Display.noInverse();
        }
        if (currentOption == ABOUT) {
            Display.inverse();
        }
        Display.print("About...");
        if (currentOption == ABOUT) {
            Display.noInverse();
        }
    } else if (currentMode == INFO)
    {
        int secondsUptime = millis() / 1000;
        int minutesUptime = secondsUptime / 60;
        secondsUptime %= 60;
        int hoursUptime = minutesUptime / 60;
        minutesUptime %= 60;

        Display.print(String(String("Uptime: ") + String(hoursUptime) + String(":") + (String(minutesUptime, DEC).length() == 1 ? "0" : "") + String(minutesUptime) + String(":") + (String(secondsUptime, DEC).length() == 1 ? "0" : "") + String(secondsUptime) + String("\n")).c_str());
        Display.print(String(String("Wi-Fi signal: ") + String(WiFi.RSSI()) + String(" dBm\n")).c_str());
        Display.print(String(String("ROM revision: ") + version).c_str());
    }

    Display.display();
}

void setup()
{
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);
    pinMode(RIGHT_BUTTON_OUT, OUTPUT);
    pinMode(LEFT_BUTTON_OUT, OUTPUT);
    pinMode(MODE_BUTTON_OUT, OUTPUT);
    pinMode(SPEAKER, OUTPUT);
    digitalWrite(RIGHT_BUTTON_OUT, HIGH);
    digitalWrite(LEFT_BUTTON_OUT, HIGH);
    digitalWrite(MODE_BUTTON_OUT, HIGH);
    left.begin();
    right.begin();
    mode.begin();

    tone(SPEAKER, 1193, 100);
    tone(SPEAKER, 1400, 100);
    tone(SPEAKER, 1700, 100);

    inputThread.enabled = true;
    inputThread.setInterval(10);
    inputThread.onRun(checkInput);
    controller.add(&inputThread);

    weatherThread.enabled = true;
    weatherThread.setInterval(1800000);
    weatherThread.onRun(getWeather);
    controller.add(&weatherThread);

    spotifyThread.enabled = false;
    spotifyThread.setInterval(1000);
    spotifyThread.onRun(runSpotify);
    controller.add(&spotifyThread);

    configThread.enabled = false;
    configThread.setInterval(100);
    configThread.onRun(runOptions);
    controller.add(&configThread);

    clockThread.enabled = true;
    clockThread.setInterval(100);
    clockThread.onRun(runClock);
    controller.add(&clockThread);

    usageThread.enabled = false;
    usageThread.setInterval(1000);
    usageThread.onRun(runUsage);
    controller.add(&usageThread);

    Display.begin();
    Display.useOffset();

    Display.draw_rectangle(32, 40, 96, 43);
    Display.display();

    Serial.println("Connecting to Wi-Fi");
    WiFi.begin(SSID, PASSWORD);
    while (WiFi.status() == WL_IDLE_STATUS) {
        delay(1000);
        Serial.print(".");
    }

    Display.draw_rectangle(32, 40, 53, 43, OLED::SOLID);
    Display.display();

    sp.begin(); // Start the webserver
    Serial.println("\nAuthenticating with\nSpotify\n");
    while (!sp.is_auth()) { // Wait for the user to authenticate
        sp.handle_client(); // Handle the client, this is necessary otherwise the webserver won't work
    }

    Display.draw_rectangle(32, 40, 74, 43, OLED::SOLID);
    Display.display();

    Serial.println("Initalizing...");
    timeClient.begin();
    preferences.begin("clock", false);
    switch (preferences.getChar("style", 0)) {
    case 0:
        style = LARGE;
        break;
    case 1:
        style = COMPACT;
        break;
    }
    brightness = preferences.getChar("brightness", 2);
    refreshWeather = preferences.getBool("weather", true);
    energySaving = preferences.getBool("energy_saving", false);
    preferences.end();
    lock(true);
    Display.draw_rectangle(32, 40, 96, 43, OLED::SOLID);
    Display.display();
    digitalWrite(LED_BUILTIN, LOW);
}

void loop()
{
    controller.run();
}

String insert_newlines(String in, int every_n)
{
    String out;
    for (int i = 0; i < in.length(); i += every_n) {
        out += in.substring(i, i + every_n);
        out += "\n";
    }
    out.remove(out.length() - 1);
    return out;
}

float lerp(float a, float b, float t)
{
    return a * (1.0 - t) + b * t;
}

void drawHand(int x, int y, int radius, float angle)
{
    int x1 = x + (int)(radius * cos(angle));
    int y1 = y + (int)(radius * sin(-angle));
    Display.draw_line(x, y, x1, y1);
}

void drawWiFiBars()
{
    long strength = WiFi.RSSI();

    if (strength < -70 && strength >= -80)
    {
        Display.draw_line(120, 7, 120, 6);
    }
    if (strength < -67 && strength >= -70)
    {
        Display.draw_line(120, 7, 120, 6);
        Display.draw_line(122, 7, 122, 4);
    }
    if (strength < -60 && strength >= -67)
    {
        Display.draw_line(120, 7, 120, 6);
        Display.draw_line(122, 7, 122, 4);
        Display.draw_line(124, 7, 124, 2);
    }
    if (strength >= -60)
    {
        Display.draw_line(120, 7, 120, 6);
        Display.draw_line(122, 7, 122, 4);
        Display.draw_line(124, 7, 124, 2);
        Display.draw_line(126, 7, 126, 0);
    }
    Serial.println(String(strength).c_str());
}
