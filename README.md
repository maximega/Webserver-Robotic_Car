CS410 Fall 2018 Final Project
Team Members: Mike Winters, Maxime Gavronsky 

Demo Video : https://www.youtube.com/watch?v=4LukunZrlmQ&feature=youtu.be

PART 1
======

Compiling and running:

	To compile binary files, do the following
		$ make

	To run our webserver, do the following
		$ ./webserv <portno>

	We will assume a port of 8080 for this documentation.

	To access content on our webserver, use a browser to navigate to the following URL
		http://localhost:8080

	To run my-histogram and get a pretty-printed output, navigate to the following URL
		http://localhost:8080/dynamic/my-histogram



Technical details:

	Our webserver includes a config file located at "./config", in which you can customize the maximum number of connections and maximum buffer size for receiving data without needing to recompile.

	Our webserver utilizes the sigaltstack method of multithreading to handle client requests.  The source for this can be found in "./threads.c" and "./threads.h"

	Our webserver is capable of serving up dynamic content that is run and generated on the backend.  To request dynamic content, the file must be in the "./dynamic/" directory.  We currently support binary files, python scripts, and perl scripts.




PART 2
======

Compiling and running:

	The environment to run our Omnibot is fairly complex.  The first step is creating a WLAN network for all of the devices to communicate across.

	To do this, first create a WPA2 AP with an SSID:loopback and PASS:robocop!
	Next, you need to connect the machine running webserv to this network, we do this in an Ubuntu VM.
	Once connected, you can run webserv and connect with a browser as specified above.
	To drive the Omnibot, you must power on the Arduino MCU on the Omni platform and wait for it to connect to the AP.
	Once the car is connected, you can navigate to "http://localhost:8080/keypress.html"
	This webpage allows the user to use W, A, S, D to move the car in 8 lateral directions, and use the left and right arrow keys to spin counter-clockwise and clockwise, respectively.



Technical details:

	Our Arduino Uno has an ESP8266 WiFi shield that allows it to connect to access points and receive data wirelessly, forwarding it to the MCU via the serial connection.  We built a small server on the Arduino to listen for incoming commands.  When they come in, our server parses them and sends the commands to the motors to drive the wheels.  The server does not send any response back to the client.  This server listens on a static IP of 192.168.2.80:8080.  The source code for this can be found at "RobotServer/RobotServer.ino". 

	To send commands to the Arduino server, we use a dynamic backend Python script on the webserver that establishes a TCP socket connection and sends 16 bytes of data.  This Python script is requested by the client every time a command needs to be sent to the Omnibot. Currently, the refresh rate for comms between the client and car is ~50ms. The source code for this can be found at "dynamic/sendCmd.py".

	The Python script is requested via Javascript code embedded in keypress.html.  When keypress.html is served to the user by our webserver, there is Javascript code that listens for keypresses and turns them into 4 motor velocity vectors (one for each motor). These 4 speeds are then sent as arguments in an HTTP GET request to the webserver for "./dynamic/sendCmd.py".  sendCmd.py does not return any data to the browser. The source code for this can be found at "keypress.html".

