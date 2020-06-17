# Gmail Apple II Email Gateway

## Motivation

To connect an Apple IIGS to Gmail, allowing messages to be sent and received.

I used Ewen Wannop (aka Speccie)'s SAM2 email client, running under GSOS 6.0.4.
This should also work with GSOS 6.0.1.

Speccie's website is [here](https://speccie.uk/software/)

In order to communicate on today's Internet Transport Layer Security (TLS)
is necessary.  Retro machines such as the Apple II series lack the processor
power to perform the necessary encryption, so it is necessary to have a proxy
system in between the Apple II and Gmail's servers.  This proxy machine can
'speak' in today's encrypted TLS protocols to Gmail, and in plaintext to our
Apple II.  I chose to use a Raspberry Pi 4 (2GB version) running the Raspbian
Linux operating system version 10.

## Prerequisites

 - An Apple IIgs.  Mine is a ROM01.
 - Enough memory  I have a 4MB RAM card.
 - Enough disk space.  I have a MicroDrive/Turbo with 32MB volumes.
 - A compatible ethernet card.  I used an Uthernet II.
 - GSOS 6.0.1 or 6.0.4 installed.
 - Marinetti 3.0 installed.  I used 3.0b11.
 - A Raspberry Pi running Raspbian 10

I don't cover any of the above in this README.  You can find information
[elsewhere](http://www.apple2.org/marinetti/) on how to set up Marinetti.

## Software Used

I use three separate packages on the Raspberry Pi, as follows:

 - *Postfix*  This is a full-featured mail tranfer agent.  We will use it
   to send mail to the Gmail servers over the SMTPS port with TLS, and to
   act as a plaintext SMTP server for the local network.
 - *Fetchmail*  Fetchmail is configured to pull down messages from a Gmail
   inbox and store it on the Raspberry Pi in `/var/mail/` using the IMAP
   protocol with TLS.
 - *Dovecot*  Dovecot provides a POP3 server to the local network, serving
   the files in `/var/mail`.

## Principle of Operation

### Incoming Messages

 - Message is sent to Gmail username@gmail.com
 - Fetchmail runs as a service on the Pi and monitors Gmail using IMAP
   IDLE.  As soon as a message shows up in the INBOX it downloads it
   and places it in `/var/mail/pi` (for username `pi`).  Fetchmail leaves
   the email on the Gmail server (this can be changed if desired.)
 - SAM2 mail client on the Apple IIgs is configured to use the IP
   of the Raspberry Pi as its POP3 email server.  When it asks for new
   messages, Dovecot will serve the request on port 110.  When messages are
   downloaded using POP3, they are deleted from `/var/mail/pi` on the
   Raspberry Pi.

### Outgoing Messages

 - The SAM2 mail client on the Apple IIgs is configured to use the IP of the 
   Raspberry Pi as its SMTP server.  Outgoing emails are sent to port 25
   on the Raspberry Pi.
 - Postfix handles the plaintext SMTP dialog with SAM2 mail and relays the
   message to Gmail's servers using SMTPS with TLS.

## Installing the Packages on Rasbian

Install the packages with root privs on the Pi:
```
sudo apt update
sudo apt upgrade
sudo apt install postfix postfix-pcre
sudo apt install dovecot-common dovecot-pop3d
sudo apt install fetchmail
```

## Obtaining App Passwords from Google

Google provides a mechanism to allow non-Google apps to connect to Gmail
called *App Passwords*.

Google's help page is [here](https://support.google.com/accounts/answer/185833?hl=en)

In order to use the App Passwords method of authentication 2-Step Verification
must be turned on for the account.  This is the general approach:

 - In a web browser log in to the Gmail account and go to
[Google Account](https://myaccount.google.com/).
 - In the panel on the left, choose *Security*.
 - Enable *2-Step Verification* for the account.
 - The option *App Passwords* will now appear. Select this option.
 - At the bottom, choose *Select app* and enter a descriptive name for
   the app.
 - Choose *Select Device* and choose the device.
 - Click *Generate*.
 - A 16 character App Password will be shown on the screen.  Write this value
   down because you will need it later in the configuration.

I generated two separate App Passwords - one for SMTPS and one for IMAPS.

## Configuring the Packages

### Postfix

The Postfix MTA configuration files are in `/etc/postfix`.  Of the three
packages, Postfix is the most complex to configure and has many available
options.

[This](https://www.linode.com/docs/email/postfix/postfix-smtp-debian7/)
page was helpful for configuring Postfix.

Be aware that this configuration amounts to an open relay from unsecured
SMTP to SMTPS, and must never be place on the public internet, or it will be
abused by spammers!  Keep it on your private LAN segment only!

We will modify a number of configuration files:

 - `/etc/postfix/command_filter`
 - `/etc/postfix/main.cf`
 - `/etc/postfix/master.cf`
 - `/etc/postfix/sasl/sasl_passwd`
 - `/etc/postfix/sasl/sasl_passwd.db`

Once Dovecot has been configured, the service may be controlled as follows:
  - `systemctl start postfix` - start service.
  - `systemctl stop postfix` - stop service.
  - `systemctl status postfix` - status of service.

#### `command_filter`

For some reason, SAM2 sends a bunch of mail headers *after* the email message
has been tranmitted to Postfix's SMTP server.  Postfix gets very unhappy about
this.  The solution is to filter them out using Postfix's
`smtpd_command_filter` function.

The `command_filter` files contains the regular expressions to filter out these
unwanted headers:
```
/^Message-ID:.*$/ NOOP
/^MIME-version:.*$/ NOOP
/^Content-Type:.*$/ NOOP
/^Content-transfer-encoding:.*$/ NOOP
/^From:.*$/ NOOP
/^To:.*$/ NOOP
/^In-Reply-To:.*$/ NOOP
/^Subject:.*$/ NOOP
/^Date:.*$/ NOOP
/^X-Mailer:.*$/ NOOP
```

#### `main.cf`

This is the main Postfix configuration file.

I adjusted `smtpd_use_tls = no` to turn off TLS for the SMTP service offered to
the Apple II and added `smtpd_command_filter =
pcre:/etc/postfix/command_filter` to activate the filter discussed above.

`relayhost = [smtp.gmail.com]:587` will forward email to Gmail's SMTPS server.

I adjusted `smtpd_relay_restrictions = permit_mynetworks
permit_sasl_authenticated defer_unauth_destination` to allow network hosts
listed in `mynetworks` to relay messages to the `relayhost`.

My home network is 192.168.10.0/24, so I added it here:
`mynetworks = 192.168.10.0/24 127.0.0.0/8 [::ffff:127.0.0.0]/104 [::1]/128`.
You should adjust this line to match your own LAN subnet.

Finally I added the following block of settings to enabled SASL authentication
when talking to Gmail:

```
# Enable SASL authentication
smtp_sasl_auth_enable = yes
# Disallow methods that allow anonymous authentication
smtp_sasl_security_options = noanonymous
# Location of sasl_passwd
smtp_sasl_password_maps = hash:/etc/postfix/sasl/sasl_passwd
# Enable STARTTLS encryption
smtp_tls_security_level = encrypt
# Location of CA certificates
smtp_tls_CAfile = /etc/ssl/certs/ca-certificates.crt

```

The whole thing looks like this:


```
# See /usr/share/postfix/main.cf.dist for a commented, more complete version


# Debian specific:  Specifying a file name will cause the first
# line of that file to be used as the name.  The Debian default
# is /etc/mailname.
#myorigin = /etc/mailname

smtpd_banner = $myhostname ESMTP $mail_name (Raspbian)
biff = no

# appending .domain is the MUA's job.
append_dot_mydomain = no

# Uncomment the next line to generate "delayed mail" warnings
#delay_warning_time = 4h

readme_directory = no

# See http://www.postfix.org/COMPATIBILITY_README.html -- default to 2 on
# fresh installs.
compatibility_level = 2

# TLS parameters
smtpd_tls_cert_file=/etc/ssl/certs/ssl-cert-snakeoil.pem
smtpd_tls_key_file=/etc/ssl/private/ssl-cert-snakeoil.key
smtpd_use_tls=no
smtpd_tls_session_cache_database = btree:${data_directory}/smtpd_scache
smtp_tls_session_cache_database = btree:${data_directory}/smtp_scache

# See /usr/share/doc/postfix/TLS_README.gz in the postfix-doc package for
# information on enabling SSL in the smtp client.

relayhost = [smtp.gmail.com]:587
smtpd_command_filter = pcre:/etc/postfix/command_filter
smtpd_relay_restrictions = permit_mynetworks permit_sasl_authenticated defer_unauth_destination
#smtpd_recipient_restrictions = permit_mynetworks
myhostname = raspberrypi.home
alias_maps = hash:/etc/aliases
alias_database = hash:/etc/aliases
mydestination = $myhostname, raspberrypi, localhost.localdomain, , localhost
mynetworks = 192.168.10.0/24 127.0.0.0/8 [::ffff:127.0.0.0]/104 [::1]/128
mailbox_size_limit = 0
recipient_delimiter = +
inet_interfaces = all
inet_protocols = all

# Enable SASL authentication
smtp_sasl_auth_enable = yes
# Disallow methods that allow anonymous authentication
smtp_sasl_security_options = noanonymous
# Location of sasl_passwd
smtp_sasl_password_maps = hash:/etc/postfix/sasl/sasl_passwd
# Enable STARTTLS encryption
smtp_tls_security_level = encrypt
# Location of CA certificates
smtp_tls_CAfile = /etc/ssl/certs/ca-certificates.crt

```

#### `master.cf`

`master.cf` does not need to be modified other than to enable `smtpd` by
uncommenting the following line:

```
# ==========================================================================
# service type  private unpriv  chroot  wakeup  maxproc command + args
#               (yes)   (yes)   (no)    (never) (100)
# ==========================================================================
smtp      inet  n       -       y       -       -       smtpd

```

If you require verbose debugging information to get the SMTP connection
working, change the line as follows:

```
smtp      inet  n       -       y       -       -       smtpd y
```

#### `sasl/sasl_passwd` and `sasl/sasl_passwd.db`

Create the directory `/etc/postfix/sasl`.

Create the file `/etc/postfix/sasl_passwd` as follows:

```
[smtp.gmail.com]:587 username@gmail.com:xxxx xxxx xxxx xxxx
```

where `username` is your Gmail account name and `xxxx xxxx xxxx xxxx` is the
App Password Google gave you.

Run: `sudo postmap /etc/postfix/sasl_passwd` to build the hash file
`sasl_passwd.db`.


 
### Dovecot

The Dovecot POP3 server configuration files are in `/etc/dovecot`. I had
to edit the following two files (starting from the default Raspbian package):

 - `/etc/dovecot/conf.d/10-auth.conf`
 - `/etc/dovecot/conf.d/10-master.conf`

Once Dovecot has been configured, the service may be controlled as follows:
  - `systemctl start dovecot` - start service.
  - `systemctl stop dovecot` - stop service.
  - `systemctl status dovecot` - status of service.

#### `10-auth.conf`

The only non-comment lines are as follows:
```
disable_plaintext_auth = no
auth_mechanisms = plain
!include auth-system.conf.ext
```

#### `10-master.conf`

I enabled the POP3 service on port 110 by uncommenting the `port = 110`
line as follows:

```
service pop3-login {
  inet_listener pop3 {
    port = 110
  }
  inet_listener pop3s {
    #port = 995
    #ssl = yes
  }
}
```
### Fetchmail

Fetchmail's configuration is in the file `/etc/fetchmail`.  It should look
like this:

```
set postmaster "pi"
set bouncemail
set no spambounce
set softbounce
set properties ""
poll imap.gmail.com with proto IMAP auth password
       user 'username' is pi here
       password 'xxxx xxxx xxxx xxxx'
       ssl, sslcertck, idle

```

Replace the `xxxx xxxx xxxx xxxx` with the App Password Google gave you. 
Replace `username` with your email account name.

Make sure the permissions on the configuration file are okay:

```
chmod 600 /etc/fetchmailrc
chown fetchmail.root /etc/fetchmailrc
```

Edit `/etc/default/fetchmail` to enable the Fetchmail service:

```
START_DAEMON=yes
```

Service controls:
  - `systemctl start fetchmail` - start service.
  - `systemctl stop fetchmail` - stop service.
  - `systemctl status fetchmail` - status of service.


## Testing

Log messages from all these packages are written to `/var/log/mail.log`.

You can test the Postfix SMTP server using `telnet`.  Be aware that it may
not work the same way from the Pi (ie: localhost) than from a different
machine on your LAN, so it is better to connect from another host.

Connect to SMTP like this `telnet raspberrypi 25`.  Typing the following
commands should send an email:

```
HELO myhost.mydomain.com
MAIL FROM:<myaccount@mydomain.com>
RCPT TO:<someotheraccount@somedomain.com>
DATA
Subject: Test message
This is just
a simple test.
.
```

The final period on its own serves to terminate the message and signal to 
Postfix that it should process the DATA block and enqueue the message.

## Configuring SAM2 Email Client on the GS

Configuring the client is simple:

 - Incoming mail via POP3
   - Hostname: hostname or IP address of your Raspberry Pi
   - Port: Default (110)
   - Username and password: Your Raspberry Pi account credentials
 - Outgoing mail via SMTP
   - Hostname: hostname or IP address of your Raspberry Pi
   - Port: Default (25)
   - Username and password: Your Raspberry Pi account credentials


Bobbi
Jun 17, 2020
*bobbi.8bit@gmail.com*


