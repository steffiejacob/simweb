Environment:
------------

    $ lsb_release -r
    Release:        14.10

    $ gcc --version
      (Ubuntu 4.9.1-16ubuntu6) 4.9.1

    $ sudo apt-get install libcurl4-openssl-dev

Build:
------

    $ make

Run:
----
    $ set env IPVERSION=6
    $ set env USER_AGENT="Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.0)"
    $ set env SIMWEB_L=1
    $ set env SK_WEBGET_DEBUG=0
    $ .build/webget [options] <URL>

OR

    $ .build/webget IPVERSION=6 USER_AGENT="Mozilla/4.0 (compatible; MSIE 8.0;Windows NT 6.0)" SIMWEB_L www.facebook.com
