#ifndef OFFICE_LIGHTS_CONFIG
#define OFFICE_LIGHTS_CONFIG

// Office Lights Configuration
//
// Here we store all of the things that make the OfficeLights app unique to a particular installation.
//  This ranges from the router we're connecting to to the SmartThings authentication strings.

// The SmartThings strings can be obtained by running the AutoOffice SmartApp:
//  https://github.com/jangellx/SmartApp-AutoOffice
const char	*stAppURL       = "https://graph.api.smartthings.com:443/api/smartapps/installations/";	// SmartThings application URL. Change this if needed
const char	*stAppID        = "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX";				// SmartThings application ID
const char	*stAccessToken  = "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX";				// SmartThings access token

const char      *sslFingerprint = "‎43 34 9a 7b a1 0a bf 96 85 79 f6 0a cc e8 fd bc 7a cb ce 33";	// HTTPS SHA1 fingerprint; look for the comment about how to generate this here: https://github.com/esp8266/Arduino/issues/2556 ... or just copy it out of the security certificate in a web browser

// The wifi state is specific to your setup.  If wifiUseStaticIP is true, then the gateway,
//  subnet mask and static IP are also required
const char	*wifiSSID     = "ssidHere";
const char	*wifiPassword = "passwrodHere";

// Static IP isn't workginf ro some reason.  I mean, it sets the static IP, the ESP8266 is ignoring
//  the DNS settings, so it can't connect to the SmartThings graph to send wake/sleep commands when
//  the button is pushed.  So for now we'll just use DHCP and set the IP statically from the router.
bool		 wifiUseStaticIP = false;								// If true, the following five must also be defined
IPAddress	 wifiIP(      192, 168,   1, 230 );
IPAddress	 wifiSubnet(  255, 255, 255,   0 );
IPAddress	 wifiGateway( 192, 168,   1,   1 );
IPAddress	 wifiDNS1(    208,  67, 222, 222 );							// OpenDNS 1
IPAddress	 wifiDNS2(    208,  67, 220, 220 );							// OpenDNS 2

// Port to run the wifi server on.
int		 serverPort = 8182;

#endif

