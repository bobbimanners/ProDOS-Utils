# deHTTPS-Proxy - Access HTTPS Websites from Retro Computers

## Purpose

Today's web is based around Transport Layer Security (TLS) protocols, namely
HTTPS.  In the last couple of years many sites that were previously available
in plaintext have been made HTTPS-only.  This renders them inaccessable to
retro computers such as the Apple II, which are not capabable of handling the
cryptography.

I had the idea of building an HTTP to HTTPS gateway, which allows requests to
be submitted in plaintext HTTP and forwards them in HTTPS, acting as a kind
of proxy.

This particular implementation `dehttps-proxy.py` is a quick and dirty hack
using Python 3.  The error handling is rather rough-and-ready but it does
work fairly well in practice.

## Configuration

Edit `dehttps-proxy.py` and set the `PORT` to the desired value.  This will
usually be port 80, the normal HTTP port.  You may also want to add any
top level domains you use to the list `tlds = [ ... ]`.  This currently just
has the common ones (`.com`, `.net`, `.gov` etc.)

If you are using port 80 you will have to run the script as root:

```
sudo ./dehttps-proxy.py
```

## Using the Proxy

I used the Contiki `WEBBROWS.SYSTEM` program on the Apple II for testing,
but you can also use a modern browser such as Chromium or Firefox.

Suppose you want to browse `https://www.example.com/path/to/page`.  Enter
the following as the URL in your browser:
`http://pi/www.example.com/path/to/page`, where `pi` is the hostname of
your Raspberry Pi (or whatever system you are using to host the proxy.)

Your request will go to port 80 (HTTP) on the server `pi` where it will be
handled by `dehttps-proxy.py`.  The Python script adds `https:/` to the path
that was passed in (`/www.example.com/path/to/page` in this instance) and
performs the HTTPS request.  The data that is obtained is returned to the
original HTTP requester in plaintext.

## Trick to Make Links Work (within a site)

Many links on webpages are internal to the site. These links are specified
using a relative rather than a fully qualified URL.  `dehttps-proxy.py` uses
a trick to make these internal links work.  When a fully qualified URL like
`www.example.com/path/to/page` is requested, the code keeps track of the
website domain `www.example.com`.  The `https://www.example.com/` prefix is
added to Subsequent relative links with URLs such as `path/to/another_page`.

This trick does not work for links to other websites.  If you click on a
link `https:://foo.com` then you will have to edit it to read
`http://pi/foo.com` and resubmit it in the browser.

## Error Handling Sucks

Error handling consists of catch the error and hope for the best.  This
could be improved.

