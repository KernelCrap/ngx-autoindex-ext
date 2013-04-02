==========================
Simple autoindex for nginx
==========================
Simple autoindex module for nginx that outputs a table of directories and files.

Warning
=======
This was written because I was not satisfied with the stock autoindex module for nginx, please be aware that there may be dragons ahead.

Requirements
============
* `nginx/1.2.x <http://nginx.org/>`__ (created for nginx/1.2.7)

Example
=======
Example config with stylesheet::

  location / {
    autoindex_ext              on;
    autoindex_ext_exact_size   off;
    autoindex_ext_stylesheet   "http://cdn.example.com/css/autoindex.css";
  }

Output
======
Example output for the above config::

  <!doctype html>
  <html lang="en">
  <head>
    <meta charset="utf-8" />
    <title>Index of /</title>
    <link rel="stylesheet" type="text/css" href="http://cdn.example.com/css/autoindex.css">
  </head>
  <body>
  <table>
    <thead>
      <tr>
        <th>Name</th>
        <th>Size</th>
      </tr>
    </thead>
    <tbody>
      <tr>
        <td><a href="example.rar">example.rar</a></td>
        <td>2100M</td>
      </tr>
      <tr>
        <td><a href="example.txt">example.txt</a></td>
        <td>1500</td>
      </tr>
    </tbody>
  </table>
  </body>
  </html>

Configuration
=============
autoindex_ext
~~~~~~~~~~~~~
:Syntax: *autoindex_ext* [*on* | *off*]
:Description:
  Enable directory listing.

autoindex_ext_exact_size
~~~~~~~~~~~~~~~~~~~~~~~~
:Syntax: *autoindex_ext_exact_size* [*on* | *off*]
:Description:
  Display the exact size of the files in bytes.

autoindex_ext_stylesheet
~~~~~~~~~~~~~~~~~~~~~~~~
:Syntax: *autoindex_ext_stylesheet uri*
:Description:
  The provided *uri* parameter will be inserted as a ``<link>`` HTML tag.