# AutoOffice_ESP8266
ESP8266 web server and client for managing my home office lights with SmartThings

This is another part of my auto-office wake/sleep setup.  Here, an Adafruit HUZZAH (which wraps an ESP8266) is wired
to a 5v 8 channel relay board and an illuminated button.  It is also connected to my LAN as a wifi client, and communicates
with SmartThings through a SmartApp.

## What It Does

This sketch handles a couple of things:
- It detects button presses. There is one illuminated button mounted on my desk.  Pressing the button turns the lights on o
off while also sending an HTTP PUT request to SmartThings to wake or sleep the other computers and turn on or off other monitors.
When the lights are off, the ring around the button slowly pulses, simlar to the sleep LED on a Mac laptop.
- It hosts a web server.  This listens for a few simple HTTP GET paths (/wake, /sleep and /status) for testing, as well as HTTP
PUT at the /do path for use by the SmartApp.

All of this means that the ESP8266 or the SmartApp (and, bye extension, any SmartThings device or computer associated with theapp)
can wake or sleep any of the computers or turn on or off any of the lights and other SmartThings-controlled devices.

## How It Works
The ESP8266 connects to my LAN via wifi.  I had some issues with getting it to conenct.
- You need external power connected; the USB power from the programming bus is not enough.  Otherwise you'll get unexpected watchdog
resets and other weird issues on boot.
- The connection status lies, and doesn't seem to return WL_IDLE_STATUS when conencting.  This means that you shouldn't try to do
auto-reconnect when disconencted yourself, instead relying on the auto-recconect feature built into the ESP8266.
- The ESP8266 remembers the wifi access points you have previosuly connected to, which can be annoying if you need to change the
access point or made a mistake when entiring the SSID and password.  I disabled auto-connect and persistence (remembering APs) so
that I could manually connect in my loop.
- I set up my code to connect manually to the AP if it is disconnected,  Since the conection status lies and reports that it's
currently disconnected while it's actually trying to connect, I would get into situations where I was unknowningly interrupting the
connection process by trying to conenct again after 2 seconds.  I fixed this by just waiting 10 seconds before testing the connection
status again, and ignoring WL_IDLE_STATUS, which should be returned when connection but appears to never occur.  Simply letting the
ESP8266 store the AP and connect on launch would have solved all these problems, but no, I had to do it myself.  I did try to manage
reconnections myself, but as mentioned above, I found it more reliable to let the ESP8266 handle this itself.
- I could get static IPs to work, but not DNS, which is important for sending requests to SmartThings.  I finally gave up and just used DHCP on the ESP8266, and assigned a static IP to it from my router by associating it with its MAC address.  Not as elegant as I'd like, but it does the job.  The static IP is needed because this is how the SmartApp finds the ESP8266.

The main loop calls different functions to handle different bits of functionality:
- webserver.handleClient() manages the web server, as normal.
- ConnectToWifi() checks to see if we're disconnected and connects to the access point.
- TestButton() checks for button presses, changing the state of the lights and sending an HTTP PUT event to SmartThings.
- DoTransition() goes to the next step in the light on or off transition.
- SleepLED(), which either keeps the button's LED ring on all the time when the lights are on, or it pulses slowly similar to a Mac
laptop's sleep LED.

## Relay Board Control
Since I had five LED panel lights, I decided that it would be more interesting to turn them on in sequence instead of all at once.
A 5v 8 channel relay board is connected to the ESP by way of some MOSFETs.  Unfortunately, while the relay board only requires 20 mA
to energize the relays, the ESP8266 can only handle about 15 mA from its pins.  The MOSFETs solve this problem.

I'm using six relays.  There are five separate LED panel power supplies.  The power supplies take up to a second to power up when
cold, which kind of ruined my lighting sequence.  To work around that, I hooked up all of the supplies's hot AC wire to the first
relay on the board.  The next five relays are hooked up to the DC output of each of the power supplies, and then to the LED panels
themselves.  The AC relay turns on first, and about a second later the lights start to flip on.  This delay is not noticable
because the wake time for the computers is a couple of seconds, so everything seems to ripple on as expected.

The transition engine is very simple, and just turns on each LED panel's associated relay after a specified time delay.  The keyframes
for the transition are stored in arrays with the light pin and transition time.  Mutliple patterns can be programmed, with different
patterns exceuted each time the lights are turned on or off.

## Configuration
All of the configuration options are in the separate `AutoOffice_ESP8266_Config.h`.  The one in GitHub is called `AutoOffice_ESP8266_Config_REMOVE_THIS_.h`, because I don't check my actual config into GitHub.  Remove the `_REMOVE_THIS_' bit, set up the variables, and you should be good to go.

## SSL
SmartThings requires SSL communicaitons.  At the time I implemented this, SSL root certificate authorities weren't supported by the
Arduino ESP8266 library.  The solution is to store the SSL fingerprint in the code and pass that when opening an HTTP connection.

This generally worked fine, until the beginning of August 2017 when the button on my desk would no longer wake the machines.  The
ESP8266 reported that het conenction had failed.  It appears that the SmartThingns certificate had been reaplced with a new one, so I
was no longer to send requests to SmartThings.  The ESP8266 would recieve requests, though, so motion sensors and the computers waking
and sleeping would trigger the lights, but the lights couldn't trigger the computers.

I "fixed" this by moving the SSH fingerprint to the config header, and copied the new fingerprint string from Chrome into the the header.  I should be good for another six months or so (the certificate seems to be good until January, anyway).  To find the fingerprint in Chrome:
- Go to graph.api.smartthings.com .
- Right click and choose Inspect.
- Go to the Security Tab.
- Click View Certificate.
- Go to the Details tab.
- Scroll down to Thumbprint, which is the fingerprint we're looking for.  Copy that string into the sslFingerprint in the config header.
- Upload the updated sketch to the ESP8266.

That took care of the problem for now.  When the Arudino library supports SSL root CA and can test the signature itself, I'll update the code so I don't have to do this every 6 months.
