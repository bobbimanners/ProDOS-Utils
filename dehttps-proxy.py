#!/usr/bin/python3

#
# Very simple prototype of an HTTP server that can perform HTTPS
# requests on behalf of a plaintext client.
#
# Bobbi June 2020
#
# If this client is running on raspberrypi:8000, then fetching URL:
# http://raspberrypi:8000/www.google.com
# Will fetch https://www.google.com
#

import http.server, socketserver
import requests, sys

baseurl = ''

PORT = 80

def isFullyQualified(name):
    tlds = [ '.com', '.org', '.net', '.gov', '.ca', '.uk', '.io', '.gs', '.click' ]
    for t in tlds:
        if name.endswith(t):
            return True
    return False

class MyHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):

    def __init__(self, req, client_addr, server):
        http.server.SimpleHTTPRequestHandler.__init__(self, req, client_addr, server)

    def do_GET(self):
        global baseurl
        if isFullyQualified(self.path):
            url = 'https:/' + self.path
            baseurl = url
        else:
            url = baseurl + self.path
        print('Getting {} ...'.format(url))
        err = False
        try:
            file = requests.get(url, allow_redirects=True)
        except:
            err = True
        if err == False:
            self.send_response(200)
            self.send_header("Content-type", "text/html")
            self.send_header("Content-length", len(file.content))
            self.end_headers()
            self.wfile.write(file.content)

print("dehttps-proxy: listening on port {} ...".format(PORT))
handler = MyHTTPRequestHandler
httpd = socketserver.TCPServer(("", PORT), handler)
httpd.serve_forever()

