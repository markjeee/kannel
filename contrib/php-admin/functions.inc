<?php

function http_send($URL,$port)
{
 $connection = fsockopen(BEARERBOX_HOST,$port,&$error_number,&$error_description,60);
 if(!$connection) {
  echo "<p>$error_description ($error_number)<br>\n";
  echo "The SMS message was NOT sent!</p>\n";
 } else {
  socket_set_blocking($connection, false);
  fputs($connection, "GET $URL HTTP/1.0\r\n\r\n");
  while (!feof($connection)) {
   $myline = fgets($connection, 128);
   switch($myline)
   {
     case (strstr($myline, 'Content-')): break;
     case (strstr($myline, 'HTTP/1')): break;
     case "": break;
     case "\r\n": break;
     default: echo "<p>".$myline."</p>";
   }
  }
  fclose ($connection);

 }
}

function octstr_append_cstr($mystring)
{
 for($pos=0;$pos<strlen($mystring);$pos++) {
  $mystring2 .= sprintf("%%%02x",$mystring[$pos]); 
 }
 return ($mystring2);
}
?>