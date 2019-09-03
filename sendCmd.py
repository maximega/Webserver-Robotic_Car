import sys, socket
'''
creates a TCP connection with the arduino server
sends the 4 arguments from the commandline to the arduino server
'''
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect(("192.168.2.80", 8080))
s.send(sys.argv[1] + sys.argv[2] + sys.argv[3] + sys.argv[4]);



