#include <DS3231.h>
#include <EmonLib.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

// Ezzel könnyebb lesz majd állítani a maximum áramerősséget.
#define AMPERAGE_MAX 1050

// The date responsible for the file naming which is holding the data.
int displayDate;

// Clock hardware module.
DS3231 Clock;

// Webserver setup for networking.
WebServer server(80);

struct TimeClock
{
	bool century, format12, pm;
} timeClock;

struct Sensor
{
	EnergyMonitor emon;
	double Irms;
} sensors[3];

struct CollectedData
{
	float min, max, sum, cnt, time;
} collectedData;

struct ProcessedData
{
	float min, max, avg;
} processedData[24];

int getCurrentDate()
{
	char getDate[9];
	sprintf(getDate, "%04d%02d%02d", 2000 + Clock.getYear(), Clock.getMonth(timeClock.century), Clock.getDate());

	return String(getDate).toInt();
}

// On connect, the device would recieve the HTML data.
void handleOnConnect()
{
	Serial.println("Connection handling called.");

	setData();							   // Aligns the data for the HTML page.
	makePage();							   // Making HTML page.
	streamFile("/index.html", "text/html"); // Streaming HTML.
}

// If the server was not found, the device would return 404.
void handleNotFound()
{
	Serial.println("Error: 404 (Not found)");
	server.send(404, "text/plain", "Not found");
}

void streamFile(const char *path, String mimeType)
{
	// Streaming the generated html file, if it exists.
	if (SPIFFS.exists(path))
	{
		File file = SPIFFS.open(path, "r");
		server.streamFile(file, mimeType);
		file.close();
	}
	else
		handleNotFound();
}

void handleToday()
{
	Serial.println("Handling current day record.");
	displayDate = getCurrentDate();
	handleOnConnect();
}

void handlePrevDay()
{
	Serial.println("Handling previous day record.");
	getPreviousDate();
	handleOnConnect();
}

void handleNextDay()
{
	Serial.println("Handling next day record.");
	getNextDate();
	handleOnConnect();
}

void handleDump()
{
	Serial.println("CSV Dump");
	streamFile(getDataFileName().c_str(), "text/plain");
}

void networkInit()
{
	// const char *WIFI_SSID = "KT Partners";
	const char *WIFI_SSID = "KT Partners";
	const char *WIFI_PASSWORD = "felavilagtetejeremama";

	Serial.println("Initializing network...");
	WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

	int retries = 0;

	while (WiFi.status() != WL_CONNECTED)
	{
		delay(1000);
		Serial.println("Establishing connection to WiFi...");

		retries++;

		if (retries >= 20)
		{
			const char *LOCAL_SSID = "ESP 32";
			const char *LOCAL_PASSWORD = "12345678";

			// Connection variable for the local network.
			IPAddress local_ip(10, 0, 0, 1);
			IPAddress gateway(10, 0, 0, 254);
			IPAddress subnet(255, 255, 255, 0);

			WiFi.softAP(LOCAL_SSID, LOCAL_PASSWORD);
			WiFi.softAPConfig(local_ip, gateway, subnet);
			delay(100);

			Serial.println("Failed connecting to outside network.");
			break;
		}
	}

	if (WiFi.status() == WL_CONNECTED)
		Serial.println(WiFi.localIP());
	else
		Serial.println(WiFi.softAPIP());

	server.on("/", handleToday);
	server.on("/next", handleNextDay);
	server.on("/prev", handlePrevDay);
	server.on("/data", handleDump);

	server.onNotFound(handleNotFound);
	server.begin();
	Serial.println("Server started.");
}

void timeInit()
{
	Serial.println("Initializing time...");

	for (int i = 0; i < 5; i++)
	{
		delay(1000);
		Serial.print(Clock.getYear(), DEC);
		Serial.print(".");
		Serial.print(Clock.getMonth(timeClock.century), DEC);
		Serial.print(".");
		Serial.print(Clock.getDate(), DEC);
		Serial.print(". ");
		Serial.print(Clock.getHour(timeClock.format12, timeClock.pm), DEC);
		Serial.print(":");
		Serial.print(Clock.getMinute(), DEC);
		Serial.print(":");
		Serial.println(Clock.getSecond(), DEC);
	}

	displayDate = getCurrentDate();
}

void checkFileExist(int date)
{
	File file = SPIFFS.open(getDataFileName(), FILE_READ);

	// * Debugging
	Serial.print("Looking for file: ");
	Serial.println(getDataFileName());

	if (file)
	{
		displayDate = date;
		Serial.print("File found: ");
		Serial.println(getDataFileName());
	}
}

void getPreviousDate()
{
	for (size_t i = displayDate; i > 20201014; i--)
		checkFileExist(i);
}

void getNextDate()
{
	for (size_t i = displayDate; i <= getCurrentDate(); i++)
		checkFileExist(i);
}

void getData()
{
	if (collectedData.time == Clock.getMinute())
		return;

	collectedData.time = Clock.getMinute();

	for (size_t i = 0; i < *(&sensors + 1) - sensors; i++)
	{
		sensors[i].Irms = random(1, 350); // * sensors[i].emon.calcIrms(1480);

		collectedData.min = sensors[i].Irms < collectedData.min ? sensors[i].Irms : collectedData.min;
		collectedData.max = sensors[i].Irms > collectedData.max ? sensors[i].Irms : collectedData.max;
		collectedData.sum += sensors[i].Irms;
	}

	if (collectedData.cnt == 5)
	{
		writeDataToCSV(collectedData.min, collectedData.max, collectedData.sum / collectedData.cnt);

		collectedData.min = AMPERAGE_MAX;
		collectedData.max = 0;
		collectedData.sum = 0;
		collectedData.cnt = 0;
	}

	collectedData.cnt++;
	Serial.println("Gotten data by the minute.");
}

void setData()
{
	memset(processedData, 0, sizeof(processedData));

	struct HourlyData
	{
		String ptr = "";
		float min = AMPERAGE_MAX;
		float max, avg, time, cnt = 1;
	} tmp;

	File dataFile = SPIFFS.open(getDataFileName());
	if (!dataFile)
		return;

	while (dataFile.available())
	{
		char fileRead = dataFile.read();

		if (fileRead == '\n')
		{
			// * 0020.10.08. 13:07,0000,0000,0000

			if (tmp.time == tmp.ptr.substring(12, 14).toInt())
			{
				tmp.min = tmp.ptr.substring(18, 22).toInt() < tmp.min ? tmp.ptr.substring(18, 22).toInt() : tmp.min;
				tmp.max = tmp.ptr.substring(23, 27).toInt() > tmp.max ? tmp.ptr.substring(23, 27).toInt() : tmp.max;
				tmp.avg = round(tmp.ptr.substring(28, 32).toInt() / tmp.cnt);
				tmp.cnt++;

				processedData[tmp.ptr.substring(12, 14).toInt()].min = tmp.min;
				processedData[tmp.ptr.substring(12, 14).toInt()].max = tmp.max;
				processedData[tmp.ptr.substring(12, 14).toInt()].avg = tmp.avg;
			}
			else
				tmp.time = tmp.ptr.substring(12, 14).toInt();

			tmp.ptr = "";
		}
		else
			tmp.ptr += fileRead;
	}

	Serial.println("Data has been set.");
}

String getDataFileName()
{
	String fileName = "/";
	fileName += displayDate;
	fileName += ".csv";

	return fileName;
}

void writeDataToCSV(int min, int max, int avg)
{
	struct DateTime
	{
		int year, month, date, hour, minute;
	} dateTime;

	// File holder for the main data file.
	File file = SPIFFS.open(getDataFileName(), FILE_APPEND);

	if (!file)
	{
		Serial.println("Failed to open file for writing.");
		return;
	}

	char dataLog[33] = "";

	dateTime.year = Clock.getYear();
	dateTime.month = Clock.getMonth(timeClock.century);
	dateTime.date = Clock.getDate();
	dateTime.hour = Clock.getHour(timeClock.format12, timeClock.pm);
	dateTime.minute = Clock.getMinute();

	// Sor formázás
	sprintf(dataLog, "%04d.%02d.%02d. %02d:%02d,%04d,%04d,%04d", 2000 + dateTime.year, dateTime.month, dateTime.date, dateTime.hour, dateTime.minute, min, max, avg);

	if (!file.println(dataLog))
		Serial.println("Writing failed.");

	Serial.println(dataLog);
	file.close();
}

String fileToString(const char *path)
{
	File file = SPIFFS.open(path);
	if (!file)
	{
		Serial.println("Failed to open file. (File to String)");
		return "";
	}

	String output = "";
	while (file.available())
	{
		char fileRead = file.read();
		output += fileRead;
	}

	return output;
}

void generateChart(File html)
{
	for (size_t i = 0; i < *(&processedData + 1) - processedData; i++)
	{
		processedData[i].min = processedData[i].min == 0 ? 1 : processedData[i].min;
		processedData[i].max = processedData[i].max == 0 ? 1 : processedData[i].max;
		processedData[i].avg = processedData[i].avg == 0 ? 1 : processedData[i].avg;

		html.print("<div class='bar1' style='--bar-value:");
		html.print(round(processedData[i].max / 1050 * 100));
		html.print("%;' data-name='");
		html.print(i);
		html.print("h' title='");
		html.print(String(processedData[i].max));
		html.print("'>");

		html.print("<div class='bar2' style='--bar-value:");
		html.print(round(processedData[i].avg / processedData[i].max * 100));
		html.print("%;' data-name='");
		html.print(i);
		html.print("h' title='");
		html.print(String(processedData[i].avg));
		html.print("'>");

		html.print("<div class='bar3' style='--bar-value:");
		html.print(round(processedData[i].min / processedData[i].avg * 100));
		html.print("%;' data-name='");
		html.print(i);
		html.print("h' title='");
		html.print(String(processedData[i].min));
		html.print("'>");

		html.print("</div></div></div>");
	}
}

// Imports the external CSS from SPIFFS.
void makeHead(File htmlFile)
{
	htmlFile.print("<head>");
	htmlFile.print("<style>");
	htmlFile.print(fileToString("/style.css"));
	htmlFile.print("</style>");
	htmlFile.print("</head>");
}

// Makes the chart for in the body.
void makeBody(File htmlFile)
{
	htmlFile.print("<body>");
	htmlFile.print("<table class='graph'>");
	htmlFile.print("<h1 style=\"margin-left: 10px; font-family: sans-serif; font-size: 25px; font-weight: bold;\">");
	htmlFile.print(displayDate);
	htmlFile.print(".");
	htmlFile.print("</h1>");
	htmlFile.print("<div class='chart-wrap vertical'><div class='grid'>");

	generateChart(htmlFile);

	htmlFile.print("</div>");

	// ! Nincsenek gombok. Bocsi :(

	htmlFile.print("<div class=\"footr\">");
	htmlFile.print("<button onclick=\"location.href='/prev'\" class='button button1' style=\"transform: translateX(20px);\">Elozo</button>");
	htmlFile.print("<button onclick=\"location.href='/'\" class='button button1' style=\"transform: translateX(70px);\">Mai nap</button>");
	htmlFile.print("<button onclick=\"location.href='/next'\" class='button button1' style=\"transform: translateX(120px);\">Kovetkezo</button>");
	htmlFile.print("<button onclick=\"location.href='/data'\" class='button2' style=\"transform: translateX(550px);\">Data</button>");
	htmlFile.print("</div>");

	htmlFile.print("</div></div>");

	htmlFile.print("</body>");
}

void makePage()
{
	Serial.println("Attempting to make HTML page.");

	File htmlFile = SPIFFS.open("/index.html", FILE_WRITE);
	if (!htmlFile)
	{
		Serial.println("Error making HTML page. (SPIFFS).");
		return;
	}

	// Making the HTML page functionally.
	htmlFile.print("<!DOCTYPE html>");
	makeHead(htmlFile);
	makeBody(htmlFile);

	htmlFile.close();
}

// Init
void setup()
{
	Serial.begin(115200);

	if (!SPIFFS.begin())
		Serial.println("Failed SPIFFS inizialization.");

	Wire.begin();

	networkInit();
	timeInit();

	sensors[0].emon.current(32, 10);
	sensors[1].emon.current(33, 10);
	sensors[2].emon.current(35, 10);
}

// Process
void loop()
{
	server.handleClient();
	getData();
}
