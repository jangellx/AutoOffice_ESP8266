/*
  AutoOffice ESP8266

  Used to control lights through a relay board.  Input is an illuminated button.  Also supports
  control through a web interface, and sends commands to a SmartThings client over HTTP.
*/

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include "AutoOffice_ESP8266_Config.h"

extern "C" {
 #include "user_interface.h"					// For setting the hostname
}

#define TELL_SMARTTHINGS_ON_BUTTON				// When defined, pressing the button will tell SmartThings to turn on/off other devices

#define BUTTON_LED_PIN			13			// LED ring on the button
#define BUTTON_PUSH_PIN			12			// The push button itself
#define LIGHT_AC_PIN			 2
#define LIGHT_1_PIN			 4
#define LIGHT_2_PIN			 5
#define LIGHT_3_PIN			15
#define LIGHT_4_PIN			14
#define LIGHT_5_PIN			16

#define DEBOUNCE_PERIOD			75			// ms to wait after a button press event to debounce

#define LED_SLEEP_PERIOD		5000			// Total amount of time the LED sleep cycle takes to run

#define LIGHTSTATE_OFF			0			// Lights are currently off
#define LIGHTSTATE_ON			1			// Lights are currently on

#define LIGHTTRANS_OFF_TO_ON_ALL_SEQ	0			// Tranition from all off to all on, sueqentially (1-5)
#define LIGHTTRANS_ON_TO_OFF_ALL_SEQ	1			// Transition from current state to all off , sueqentially (5-1)
#define LIGHTTRANS_OFF_TO_ON_ALL_CENTER	2			// Tranition from all off to all on, from center (3, 2&4, 1&5)
#define LIGHTTRANS_ON_TO_OFF_ALL_CENTER	3			// Transition from current state to all off, from center (1&5, 2&4, 3)
#define LIGHTTRANS_OFF_TO_ON_135	4			// Tranition from all off to 1, 3 and 5 on
#define LIGHTTRANS_COUNT                5			// Total number of transitions
#define LIGHTTRANS_NONE			-1			// No transition currently running.  Also used to terminate arrays

int	toAllOnTransitions[]  = { LIGHTTRANS_OFF_TO_ON_ALL_SEQ, LIGHTTRANS_OFF_TO_ON_ALL_CENTER, LIGHTTRANS_NONE };		// All "off to all on" transitions
int	toAllOffTransitions[] = { LIGHTTRANS_ON_TO_OFF_ALL_SEQ, LIGHTTRANS_ON_TO_OFF_ALL_CENTER, LIGHTTRANS_NONE };		// All "on to all off" transitions
int	toAllIndex            = 0;				// Index into the on/off transition array to fire next

int  lightState           = LIGHTSTATE_OFF;			// Current state of the lights
int  transitionTo         = LIGHTSTATE_ON;			// State we're transitioning to
int  transitionStart      = 0;					// Time that the transition from the previous state to 
int  onTransition         = LIGHTTRANS_OFF_TO_ON_ALL_SEQ;	// State to transition to when all lights are off (not currently used)

char relayStates[6]    = { 0, 0, 0, 0, 0, 0 };			// Current on/off state of all the lights
int  lightPins[6];						// Array to map indices to light pins
int  lightIndices[16];						// Array to map pins back to indices

#define  TRANSITION_DELAY		500			// ms between keys in a transition

// A signle keyframe in a transition
typedef struct st_TransitionKey {
	int		 lightPin;				// Pin for this light
	bool		 willBeOn;				// State of the light after the transition completes
	int		 ms;					// Time in milliseconds to wait since the previous key to fire this key
} TransitionKey;

// A complete transition.  Most of the state is initialized during setup
typedef struct st_Transition {
	int		 runtime;				// Total runtime (computed from keys during setup)
	int		 keyCount;				// Total number of keys in the transtion (computed during setup)
	TransitionKey	*keys;					// Array of TRANSITION_END-terminated keys (set during setup)
} Transition;

Transition	 transitions[     LIGHTTRANS_COUNT ];		// The different transitions
const char	*transitionNames[ LIGHTTRANS_COUNT ];		// Names of the transitions for debugging

#define TRANSITION_END			-1			// Marks the end of a TransitionKey arra

// Transition all the lights to off, from 6 to 1.
TransitionKey transition_OnToOffAll_Seq[] = {
/*	{ LIGHT_5_PIN, false, TRANSITION_DELAY },
	{ LIGHT_5_PIN, true,  TRANSITION_DELAY },
	{ LIGHT_4_PIN, false, TRANSITION_DELAY },
	{ LIGHT_4_PIN, true,  TRANSITION_DELAY },
	{ LIGHT_3_PIN, false, TRANSITION_DELAY },
	{ LIGHT_3_PIN, true,  TRANSITION_DELAY },
	{ LIGHT_2_PIN, false, TRANSITION_DELAY },
	{ LIGHT_2_PIN, true,  TRANSITION_DELAY },
	{ LIGHT_1_PIN, false, TRANSITION_DELAY },
	{ LIGHT_1_PIN, true,  TRANSITION_DELAY },
*/
	{ LIGHT_5_PIN, false, TRANSITION_DELAY },
	{ LIGHT_4_PIN, false, TRANSITION_DELAY },
	{ LIGHT_3_PIN, false, TRANSITION_DELAY },
	{ LIGHT_2_PIN, false, TRANSITION_DELAY },
	{ LIGHT_1_PIN, false, TRANSITION_DELAY },
	{ TRANSITION_END }
};

// Transition all lights on, from 1 to 6
TransitionKey transition_OffToOnAll_Seq[] = {
/*	{ LIGHT_1_PIN, true,  TRANSITION_DELAY },
	{ LIGHT_1_PIN, false, TRANSITION_DELAY },
	{ LIGHT_2_PIN, true,  TRANSITION_DELAY },
	{ LIGHT_2_PIN, false, TRANSITION_DELAY },
	{ LIGHT_3_PIN, true,  TRANSITION_DELAY },
	{ LIGHT_3_PIN, false, TRANSITION_DELAY },
	{ LIGHT_4_PIN, true,  TRANSITION_DELAY },
	{ LIGHT_4_PIN, false, TRANSITION_DELAY },
	{ LIGHT_5_PIN, true,  TRANSITION_DELAY },
	{ LIGHT_5_PIN, false, TRANSITION_DELAY },
*/
	{ LIGHT_1_PIN, true,  TRANSITION_DELAY },
	{ LIGHT_2_PIN, true,  TRANSITION_DELAY },
	{ LIGHT_3_PIN, true,  TRANSITION_DELAY },
	{ LIGHT_4_PIN, true,  TRANSITION_DELAY },
	{ LIGHT_5_PIN, true,  TRANSITION_DELAY },
	{ TRANSITION_END }
};

// Transition all lights on, from 1 to 6
TransitionKey transition_OffToOnAll_Center[] = {
/*	{ LIGHT_3_PIN, true,  TRANSITION_DELAY },
	{ LIGHT_3_PIN, false, TRANSITION_DELAY },
	{ LIGHT_2_PIN, true,  TRANSITION_DELAY },	{ LIGHT_4_PIN, true,  0 },
	{ LIGHT_2_PIN, false, TRANSITION_DELAY },	{ LIGHT_4_PIN, false, 0 },
	{ LIGHT_1_PIN, true,  TRANSITION_DELAY },	{ LIGHT_5_PIN, true,  0 },
	{ LIGHT_1_PIN, false, TRANSITION_DELAY },	{ LIGHT_5_PIN, false, 0 },
*/
	{ LIGHT_3_PIN, true,  TRANSITION_DELAY },
	{ LIGHT_2_PIN, true,  TRANSITION_DELAY },	{ LIGHT_4_PIN, true,  0 },
	{ LIGHT_1_PIN, true,  TRANSITION_DELAY },	{ LIGHT_5_PIN, true,  0 },
	{ TRANSITION_END }
};

// Transition all lights on, from 1 to 6
TransitionKey transition_OnToOffAll_Center[] = {
/*	{ LIGHT_1_PIN, false, TRANSITION_DELAY },	{ LIGHT_5_PIN, false, 0 },
	{ LIGHT_1_PIN, true,  TRANSITION_DELAY },	{ LIGHT_5_PIN, true,  0 },
	{ LIGHT_2_PIN, false, TRANSITION_DELAY },	{ LIGHT_4_PIN, false, 0 },
	{ LIGHT_2_PIN, true,  TRANSITION_DELAY },	{ LIGHT_4_PIN, true,  0 },
	{ LIGHT_3_PIN, false, TRANSITION_DELAY },
	{ LIGHT_3_PIN, true,  TRANSITION_DELAY },
*/
	{ LIGHT_1_PIN, false, TRANSITION_DELAY },	{ LIGHT_5_PIN, false,  0 },
	{ LIGHT_2_PIN, false, TRANSITION_DELAY },	{ LIGHT_4_PIN, false,  0 },
	{ LIGHT_3_PIN, false, TRANSITION_DELAY },
	{ TRANSITION_END }
};


// Transition all lights 1, 3 and 5
TransitionKey transition_OffToOn135[] = {
/*	{ LIGHT_1_PIN, true,  TRANSITION_DELAY },
	{ LIGHT_1_PIN, false, TRANSITION_DELAY },
	{ LIGHT_2_PIN, false, TRANSITION_DELAY },
	{ LIGHT_3_PIN, true,  TRANSITION_DELAY },
	{ LIGHT_3_PIN, false, TRANSITION_DELAY },
	{ LIGHT_4_PIN, false, TRANSITION_DELAY },
	{ LIGHT_5_PIN, true,  TRANSITION_DELAY },
	{ LIGHT_5_PIN, false, TRANSITION_DELAY },
*/
	{ LIGHT_1_PIN, true,  TRANSITION_DELAY },
	{ LIGHT_2_PIN, false, TRANSITION_DELAY },
	{ LIGHT_3_PIN, true,  TRANSITION_DELAY },
	{ LIGHT_4_PIN, false, TRANSITION_DELAY },
	{ LIGHT_5_PIN, true,  TRANSITION_DELAY },
	{ TRANSITION_END }
};

// Web server state
ESP8266WebServer	 webServer( serverPort );

#define WIFI_CONNECT_RETRY_DELAY		10000	// How long to wait before trying to conenct to wifi again, if auto-recoonect doesn't do it for us

// Connect to wifi.  Two things we learned here:
//  - For some reason, WL_IDLE_STATUS is a lie, and we're getting it when we dont' expect it
//  - For some reason, while attempting to connected we're getting WL_DISCCONECTED instead of
//    WL_IDLE_STATUS, so we keep canceling and trying to connect again.
// For this reason, we ignore the IDLE_STATE and use a 10 second delay to try to reconnect.
//  This seems to do the trick... for now...
// Also, we enabled auto-reconnect.  That means we shouldn't need to try to reconnect ourselves,
//  but if we see that we're not connected we'll try to do that anyway.
void ConnectToWifi() {
	static int	 lastConnectAttempt = 0;
	static bool	 wasConnected       = false;
	int		 curStatus          = WiFi.status();

	// Check the status.  If we're connected, there's nothing to do.
	if( curStatus == WL_CONNECTED ) {
		if( !wasConnected ) {
			Serial.print(  "Wifi connected with ip " );
			Serial.println( WiFi.localIP() );

			// Print the MAC address of the ESP8266
			byte mac[6];
			WiFi.macAddress(mac);
			Serial.print("MAC Address: ");
			Serial.print(mac[5],HEX);
			Serial.print(":");
			Serial.print(mac[4],HEX);
			Serial.print(":");
			Serial.print(mac[3],HEX);
			Serial.print(":");
			Serial.print(mac[2],HEX);
			Serial.print(":");
			Serial.print(mac[1],HEX);
			Serial.print(":");
			Serial.println(mac[0],HEX);

			wasConnected = true;
		}

		return;
	}

	// If we're trying to conenct (IDLE_STATUS), we just return
//	if( curStatus == WL_IDLE_STATUS )
//		return;

	// Wait a bit before trying to connect, if necessary
	if( millis() < (lastConnectAttempt + WIFI_CONNECT_RETRY_DELAY) ) 
		return;

	lastConnectAttempt = millis();

	// Report status
	if( curStatus == WL_DISCONNECTED )	Serial.println( "Wifi disconnected." );
	if( curStatus == WL_CONNECT_FAILED )	Serial.println( "Wifi connection failed." );
	if( curStatus == WL_CONNECTION_LOST )	Serial.println( "Wifi connection lost." );
	
	// If we got this far, we try to conenct to the access point access point
	Serial.print( "Connecting to wifi access point " );
	Serial.print( wifiSSID );
	Serial.println( "..." );

//	WiFi.hostname( "OfficeLights" );			// Doesn't work for whatever reason
	wifi_station_set_hostname( "OfficeLights" );
//	WiFi.mode(   WIFI_STA );
	WiFi.begin(  wifiSSID, wifiPassword );

	if( wifiUseStaticIP )
		WiFi.config( wifiIP, wifiSubnet, wifiGateway, wifiDNS1, wifiDNS2 );

//	WiFi.printDiag( Serial );
}

// Send a response to the server containing the "isAwake" state as JSON.
void RespondWithAwakeStateJSON( bool state ) {
	DynamicJsonBuffer	 jsonBuffer;					// Must be declared here (as opposed to globally), or we run out of memory and stop parsing eventually ( https://bblanchon.github.io/ArduinoJson/faq/the-first-parsing-succeeds-why-do-the-next-ones-fail/ )

	JsonObject		&root = jsonBuffer.createObject();
	root["isAwake"] = state ? 1 : 0;

	char			 buf[256];
	root.printTo( buf, 256 );

	webServer.send( 200, "applicationt/json", buf );
}

// Set up the web server and the paths we handle.
void SetupWebServer() {
	webServer.on( "/", [](){
		// Just display the status
		String	string = "OfficeLights Server Running.\n\n";
		string += "Lights are currently " + String( (lightState == LIGHTSTATE_OFF) ? "on" : "off" ) + ".";

		webServer.send( 200, "text/html", string );
	});

	webServer.on( "/do", HTTP_PUT, [](){
		// PUT request to change the light state based on JSON state
		DynamicJsonBuffer	 jsonBuffer;				// Must be declared here (as opposed to globally), or we run out of memory and stop parsing eventually ( https://bblanchon.github.io/ArduinoJson/faq/the-first-parsing-succeeds-why-do-the-next-ones-fail/ )
		JsonObject		&parsed = jsonBuffer.parseObject( webServer.arg(0) );

		if( !parsed.success() ) {
			Serial.print( "/do request; error parsing JSON:  " );
			Serial.print( webServer.arg(0) );
			Serial.println();

		} else {
			const char	*command = parsed["command"];
			if( command == NULL ) {
				Serial.println( "/do request; command not found." );

			} else if( strcmp( command, "wake" ) == 0 ) {
				TurnOnLights( true );
				Serial.println( "/do request; wake - lights on." );

			} else if( strcmp( command, "sleep" ) == 0 ) {
				TurnOnLights( false );
				Serial.println( "/do request; sleep - lights off." );

			} else {
				Serial.println( "/do request; unknown command." );
			}
		}

		RespondWithAwakeStateJSON( lightState == LIGHTSTATE_ON );
	});

	webServer.on( "/sleep", HTTP_GET, [](){
		// Sleep
		if( lightState == LIGHTSTATE_OFF ) {
			Serial.println( "/sleep request; lights already off, nothing to do." );
		} else {
			TurnOnLights( false );
	
			Serial.print(   "/sleep request; transitioning to " );
			Serial.print(   transitionNames[ transitionTo ] );
			Serial.println( "..." );
		}

		RespondWithAwakeStateJSON( false );
	});

	webServer.on( "/wake", HTTP_GET, [](){
		// Wake
		if( lightState == LIGHTSTATE_ON ) {
			Serial.println( "/wake request; lights already on, nothing to do." );
		} else {
			TurnOnLights( true );
	
			Serial.print(   "/wake request; transitioning to " );
			Serial.print(   transitionNames[ transitionTo ] );
			Serial.println( "..." );
		}

		RespondWithAwakeStateJSON( true );
	});

	webServer.on( "/status", HTTP_GET, [](){
		// Status as JSON
		Serial.println( "/status request." );

		RespondWithAwakeStateJSON( lightState == LIGHTSTATE_ON );
	});

	webServer.begin();

	Serial.println( "Web server started." );
}

// Initialization
void setup() {
	// Watchdog timer seems to kick in if startup takes >1 second, so it mostly just gets in the way
	int		 i, j, runtime;

	ESP.wdtDisable();
	delay(0);

	WiFi.persistent(       false );
	WiFi.setAutoConnect(   false );
	WiFi.setAutoReconnect( true  );

	// Set up the serial port
	Serial.begin( 115200 );

	Serial.println( "Setting up..." );

	// Set up some variables.  We could do it above, but this is more clear and less prone to error if we change the defines
	lightIndices[ LIGHT_1_PIN ] = 0;	lightPins[0] = LIGHT_1_PIN;
	lightIndices[ LIGHT_2_PIN ] = 1;	lightPins[1] = LIGHT_2_PIN;
	lightIndices[ LIGHT_3_PIN ] = 2;	lightPins[2] = LIGHT_3_PIN;
	lightIndices[ LIGHT_4_PIN ] = 3;	lightPins[3] = LIGHT_4_PIN;
	lightIndices[ LIGHT_5_PIN ] = 4;	lightPins[4] = LIGHT_5_PIN;

	transitions[ LIGHTTRANS_ON_TO_OFF_ALL_SEQ ].keys    = transition_OnToOffAll_Seq;	transitionNames[ LIGHTTRANS_ON_TO_OFF_ALL_SEQ    ] = "On to All Off (sequential)";
	transitions[ LIGHTTRANS_OFF_TO_ON_ALL_SEQ ].keys    = transition_OffToOnAll_Seq;	transitionNames[ LIGHTTRANS_OFF_TO_ON_ALL_SEQ    ] = "Off to All On (sequential)";
	transitions[ LIGHTTRANS_ON_TO_OFF_ALL_CENTER ].keys = transition_OnToOffAll_Center;	transitionNames[ LIGHTTRANS_ON_TO_OFF_ALL_CENTER ] = "On to All Off (center)";
	transitions[ LIGHTTRANS_OFF_TO_ON_ALL_CENTER ].keys = transition_OffToOnAll_Center;	transitionNames[ LIGHTTRANS_OFF_TO_ON_ALL_CENTER ] = "Off to All On (center)";
	transitions[ LIGHTTRANS_OFF_TO_ON_135 ].keys        = transition_OffToOn135;		transitionNames[ LIGHTTRANS_OFF_TO_ON_135        ] = "Off to 1/3/5 On";

	Serial.println( "Computing key transition runtimes..." );
	// Compute key transition runtimes
	for( i=0; i < LIGHTTRANS_COUNT; i++ ) {
		runtime = 0;

		for( j=0; /*NULL*/; j++ ) {
			if( transitions[i].keys[j].lightPin == TRANSITION_END )
				break;

			runtime += transitions[i].keys[j].ms;
		}

		transitions[i].keyCount = j;
		transitions[i].runtime  = runtime;
	}

	Serial.println( "Iniitalizing pins..." );
	// Initialize the button pins
	pinMode( BUTTON_LED_PIN,  OUTPUT       );	// Initialize the button's LED as output
	pinMode( BUTTON_PUSH_PIN, INPUT_PULLUP );	// Pullup the push button pin to avoid floating inputs

	pinMode( 0,  OUTPUT                    );	// Initialize the LED_BUILTIN pin as an output

	// Initilaize the LED pins as outputs
	pinMode( LIGHT_1_PIN,   OUTPUT         );
	pinMode( LIGHT_2_PIN,   OUTPUT         );
	pinMode( LIGHT_3_PIN,   OUTPUT         );
	pinMode( LIGHT_4_PIN,   OUTPUT         );
	pinMode( LIGHT_5_PIN,   OUTPUT         );
	pinMode( LIGHT_AC_PIN,  OUTPUT         );

	// Start the web server
	SetupWebServer();

	// Force disconnect the wifi
	WiFi.disconnect(false);

	// Re-enable the watchdog timer.  If it's off for >6 seconds, the hardware watchdog kicks in
	ESP.wdtEnable(4000);

	Serial.println( "Setup complete." );
	Serial.println( "" );
}

// Toggle the state of the lights
void TurnOnLights( bool state ) {
	// Make sure there's something to do
	if( transitionTo != LIGHTTRANS_NONE ) {
		Serial.println( " - Currently transitioning; nothing to do" );
		return;
	}
	if( (lightState == LIGHTSTATE_ON) && state ) {
		Serial.println( " - Lights already on; nothing to do" );
		return;
	}

	if( (lightState == LIGHTSTATE_OFF) && !state ) {
		Serial.println( " - Lights already off; nothing to do" );
		return;
	}

	if( state ) {
		lightState   = LIGHTSTATE_ON;
		transitionTo = toAllOnTransitions[ toAllIndex ];

	} else {
		lightState   = LIGHTSTATE_OFF;
		transitionTo = toAllOffTransitions[ toAllIndex ];

		toAllIndex++;
		if( toAllOnTransitions[ toAllIndex ] == LIGHTTRANS_NONE )
			toAllIndex = 0;
	}

	transitionStart = millis();
}

// Handle any changes to the button.  We require a DEBOUNCE_PERIOD amount of time between
//  state changes (depressed to/from released), just in case.
void TestButton () {
	static bool	stateChanged     = false;
	static int	stateChangedTime = 0;
	
//	Serial.println( "TestButton()" );

	if( (millis() - stateChangedTime) < DEBOUNCE_PERIOD )
		return;

	// Test for a button press
	if( (digitalRead( BUTTON_PUSH_PIN ) == LOW) ) {
		if( !stateChanged ) {
			Serial.print(   "Button pressed; transitioning to " );
			Serial.print(   transitionNames[ transitionTo ] );
			Serial.println( "..." );

			#ifdef TELL_SMARTTHINGS_ON_BUTTON
				// Send the request to SmartThings.  Since we haven't changed lightState yet, we have to
				//  flip the tests (ie: if LIGHTSTATE_OFF, send "wake", not "sleep").

				// Set up our JSON payload
				DynamicJsonBuffer	 jsonBuffer;					// Must be declared here (as opposed to globally), or we run out of memory and stop parsing eventually ( https://bblanchon.github.io/ArduinoJson/faq/the-first-parsing-succeeds-why-do-the-next-ones-fail/ )
				char			 buf[256];
				JsonObject		&root = jsonBuffer.createObject();
	
				root["command"] = (lightState == LIGHTSTATE_OFF) ? "wake" : "sleep";
				root.printTo( buf, 256 );
	
				// Send the PUT request to SmartThings
				String		urlEnd = String( stAppID ) + String( "/do" ) + String( (lightState == LIGHTSTATE_OFF) ? "/wake" : "/sleep");
				String		url    = String( stAppURL ) + urlEnd;
				Serial.print(   " - Sending \"do\" action to SmartThings at " );
				Serial.println( url );
	
				HTTPClient	http;
				http.begin( url, sslFingerprint );
				http.addHeader( "Authorization", String("Bearer ") + stAccessToken );
	
				Serial.print(   " - HTTP PUT payload: " );
				Serial.println( buf );
	
				int error = http.sendRequest( "PUT", buf );				// IDE can't find HTTPClient::PUT() for some reason, so we'll just do it ourselves
				if( error < 0 ) {
					Serial.print(   " - HTTP PUT request failed:  " );
					Serial.println( http.errorToString( error ) );
					Serial.println( "   For connection refused, check HTTPS SHA1 fingerprint, as SSL credentials at smarthings.com might have changed" );
				} else {
					Serial.print(   " - HTTP response:  " );
					Serial.println( error );
					Serial.println( " ------------------------------ " );
					Serial.println( http.getString() );
					Serial.println( " ------------------------------ " );
				}
	
				http.end();
			#endif

			// Turn the lights on/off.  We have to do this after the HTTP request or we screw up
			//  the transition timing (the first two will usually fire immediately after each other).
			stateChanged     = true;
			stateChangedTime = millis();

			// Actually turn the lights on/off
			TurnOnLights( (lightState == LIGHTSTATE_ON) ? false : true );
		}

	} else {
		stateChanged = false;
	}
}

// Do the current transition.  If we don't have a transition, we just return.
//  If we do, we loop through the keys and fire every key that is before the
//  current transition time that we haven't fired yet.  Once we have exceeded
//  the transition time, we must have finished so we clera the transition state.
#define AC_POWER_UP_DELAY			  1000	// 1 second; ms to wait after turning on the AC pin before doing a power on transition
#define AC_WARM_POWER_UP_DELAY			   200	// 1/5 second; ms to wait after turning on the AC pin if we recently powered off
#define AC_POWER_DOWN_WARM_PERIOD		600000	// 1 minute;  how long since power down we consider the power supplies to be "warm" and not require a long power up pweriod

static int		powerDownAt = 0;		// Time (in ms) that we last turned off the AC pin.  Used to detect warm vs cold power up

void DoTransition() {
	int		 now          = millis();
	int		 keyTime      = 0;		// Current time of the key in the animation (cumulative time of all keys including this one)
	int		 transTime;			// Current time in the transition (now - transitionStart)
	int		 i, warmUpDelay;
	static int	 currentKey   = 0;		// Current key to be processed; 0 if we're starting the transition (first key)
	static bool	 reportWarmUp = true;
	Transition	*transition;
	TransitionKey	*key;

	if( transitionTo == LIGHTTRANS_NONE )
		return;

	transition = &(transitions[ transitionTo ] );
	transTime  = now - transitionStart;

	if( lightState == LIGHTSTATE_ON ) {
		// Turn on the AC power pin, and adjust the start time by AC_POWER_UP_DELAY or AC_WARM_POWER_UP_DELAY
		warmUpDelay = (transitionStart - powerDownAt) < AC_POWER_DOWN_WARM_PERIOD ? AC_WARM_POWER_UP_DELAY : AC_POWER_UP_DELAY;
		transTime  -= warmUpDelay;				// Cold vs warm power up; we have to wait longer for the power supplies to be ready

		if( reportWarmUp ) {
			reportWarmUp = false;
			Serial.print( "Transition: power supply warm up (" );
			Serial.print( warmUpDelay );
			Serial.println( " ms)" );
		}
		digitalWrite( LIGHT_AC_PIN, HIGH );
	}

	// Loop through the keys, firing any between that are at least currentKey,
	//  and which are also less than the current transTime.  This properly
	//  accounts for two keys that fire at the same time (0 delay).
	for( i=0; i < transition->keyCount; i++ ) {
		key      = &(transition->keys[i]);
		keyTime += key->ms;

		if( keyTime > transTime )
			break;

		if( i < currentKey )
			continue;

		digitalWrite( key->lightPin, key->willBeOn ? HIGH : LOW );
	}

	if( i > currentKey )
		currentKey = i;

	if( transTime >= transition->runtime ) {
		// Done; clear the transition
		if( lightState == LIGHTSTATE_OFF ) {
			// Turn off the AC pin and store the time we shut it down
			digitalWrite( LIGHT_AC_PIN, LOW );
			powerDownAt = millis();
		}

		reportWarmUp = true;
		transitionTo = LIGHTTRANS_NONE;
		currentKey   = 0;
		Serial.println( "Transition complete." );
	}
}

// the loop function runs over and over again forever
void loop() {
	delay( 25 );

	// Handle any incoming HTTP clients
	webServer.handleClient();

	// Connect (or re-conenct) to wifi, if necessary
	ConnectToWifi();

	// Test the button, which sets up the transition to turn the lights on and off
	TestButton();

	// Update transition
	DoTransition();

	// Update the button LED state
	if( lightState == LIGHTSTATE_OFF )
		SleepLED( BUTTON_LED_PIN );
	else
		analogWrite( BUTTON_LED_PIN, PWMRANGE );
}

// Pulse the LED at a slowly (5 seconds) to indicate a sleep state
void SleepLED( int pin ) {
	static time_t	startTime = 0;
	time_t		n         = millis();

	if( ((startTime == 0) || (n - LED_SLEEP_PERIOD) > startTime) )
		startTime = n;

	int brightness = ((n - startTime) / (float)LED_SLEEP_PERIOD) * PWMRANGE;
	if( brightness > PWMRANGE/2 )
		brightness = (PWMRANGE/2) - (brightness - (PWMRANGE/2));

/*	Serial.print(   "Brightness: " );
	Serial.print(   brightness     );
	Serial.print(   "  n: "        );
	Serial.print(   n              );
	Serial.print(   "  starTime: " );
	Serial.print(   startTime      );
	Serial.print(   " (" );
	Serial.print(   n - startTime  );
	Serial.println( ")" );
*/
	analogWrite( pin, brightness );
}


