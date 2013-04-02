==========================
Simple autoindex for nginx
==========================
Simple autoindex module for nginx that outputs a table of directories and files.

Warning
=======
Please be aware that there may be dragons ahead.

Requirements
============
* `nginx/1.2.x <http://nginx.org/>`__ (created for nginx/1.2.7)

Examples
========
Example config::

  location / {
    autoindex_ext              on;
    autoindex_ext_exact_size   off;
    autoindex_ext_stylesheet   "http://cdn.example.com/css/autoindex.css";
  }

Another example (directory)::

  location /example {
    alias                      /var/www/public;
    autoindex_ext              on;
    autoindex_ext_exact_size   off;
    autoindex_ext_stylesheet   "http://cdn.example.com/css/autoindex.css";
  }

Output
======
Example output for the above configs::

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

Example stylesheet:
===================
An example of how the table could be styled::

  body, html {
    background: #FFFFFF;
    font-family: Consolas, Courier New, monospace, serif;
    font-size: 18px;
  }
  table {
    border: 1px solid #999;
    width: 100%;
  }
  th, td {
    padding: 0.1em;
    padding-left: 0.5em;
  }
  th {
    background: #F6F6F6;
    text-align: left;
  }
  tr:nth-child(even) {
    background: #F6F6F6;
  }
  a, a:visited {
    color: #08C;
    text-decoration: none;
  }
  a:hover,a:focus{
    color: #005580;
    text-decoration: underline
  }

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
