<html>
<head>
<title>SMS OTA Message Sender</title>
</head>
<body bgcolor="#FFFFFF" text="#000000">

<?php
include("config.inc");
include("functions.inc");

if ($submit)
{
 $text  = "%01%06%04%03%94%81%EA%00%01%45%C6%06%01%87%12";
 $text .= (strcasecmp($connection,"data") == 0) ? "%45" : "%FF";
 $text .= "%01";
 $text .= "%87%13%11%03";
 $text .= octstr_append_cstr($otaip);
 $text .= "%00%01";
 $text .= "%87%14";
 $text .= (strcasecmp($bearer,"Cont") == 0) ? "%60" : "%61";
 $text .= "%01";
 $text .= "%87%21%11%03";
 $text .= octstr_append_cstr($otaphone);
 $text .= "%00%01";
 $text .= "%87%22";
 $text .= (strcasecmp($otauth,"secure") == 0) ? "%71" : "%70"; 
 $text .= "%01";
 $text .= "%87%23%11%03";
 $text .= octstr_append_cstr($otalogin);
 $text .= "%00%01";
 $text .= "%87%24%11%03";
 $text .= octstr_append_cstr($otapassword);
 $text .= "%00%01";
 $text .= "%87%28";
 $text .= (strcasecmp($calltype,"isdn") == 0) ? "%73" : "%FF"; 
 $text .= "%01";
 $text .= "%87%29";
 $text .= (strcasecmp($speed,"9600") == 0) ? "%6B" : "%6C"; 
 $text .= "%01%01";
 $text .= "%86%07%11%03";
 $text .= octstr_append_cstr($otalocation);
 $text .= "%00%01";
 $text .= "%C6%08%01";
 $text .= "%87%15%11%03";
 $text .= octstr_append_cstr($otaservice);
 $text .= "%00%01";
 $text .= "%01%01";

 $URL = "/cgi-bin/sendsms?username=".USERNAME."&password=".PASSWORD."&from=".GLOBAL_SENDER."&to=$to&udh=%06%05%04%C3%4F%C0%02&text=$text";
 echo "Sending the OTA SMS Text message <b>\"$text\"</b> (length: ".strlen($text).") to the phone <b>$to</b>...<br>\n";
  http_send($URL,SENDSMS_PORT);
  echo "<address><a href=\"$PHP_SELF\">Back to Send OTA SMS Message</a></address>\n";
         
} else {

?>

<h1>OTA SMS Message Sender</h1>
<form name="sendota" method="post" action="<?php echo "$PHP_SELF" ?>">
<p>
Telephone number:
<br>
<input type="text" size="30" name="to">
</p>
<p>
Configuration:
<table border=0>
<tr><td>Location:</td><td><input type="text" size="30" name="otalocation"> <i>(ie. http://wap.yoursite.com)</i></td></tr>
<tr><td>Service Name:</td><td><input type="text" size="30" name="otaservice"> <i>(ie. You Wapsite)</i></td></tr>
<tr><td>IP address:</td><td><input type="text" size="30" name="otaip"> <i>(ie. 192.168.1.1)</i></td></tr>
<tr><td>Phone number:</td><td><input type="text" size="30" name="otaphone"> <i>(ie. 4512345678)</i></td></tr>
<tr><td>Bearer type:</td><td><select name="bearer"><option name="data" selected>data<option name="sms">SMS</select> <i>(ie. data)</i></td></tr>
<tr><td>Connection type:</td><td><select name="connection"><option name="temp" selected>Temp<option name="cont">cont</select> <i>(ie. temp)</i></td></tr>
<tr><td>Call type:</td><td><select name="calltype"><option name="ISDN" selected>ISDN<option name="Analog">Analog</select> <i>(ie. ISDN)</i></td></tr>
<tr><td>Speed:</td><td><select name="speed"><option name="9600" selected>9600<option name="14400">14400</select> <i>(ie. 9600)</i></td></tr>
<tr><td>PPP security:</td><td><select><option name="Off" selected>Off<option name="On">On</select> <i>(ie. Off)</i></td></tr>
<tr><td>Authentication:</td><td><select name="otaauth"><option name="Normal" selected>Normal<option name="Secure">Secure</select> <i>(ie. Normal)</i></td></tr>
<tr><td>Login:</td><td><input type="text" size="30" name="otalogin"> <i>(ie. login)</i></td></tr>
<tr><td>Password:</td><td><input type="password" size="30" name="otapassword"> <i>(ie. secret)</i></td></tr>
</table>
</p>
<input type="submit" value="Send Message" name="submit">
<input type="reset" value="Reset">
<br>
</form>

<?php
}
?>

<p>
</p>
<hr>
<table border="0" width="100%">
  <tr>
    <td width="50%"><address><a href="index.html">Back to admin</a></address></td>
    <td width="50%" align="right"><address>Visit the Kannel homepage at <a href="http://www.kannel.org">www.kannel.org</a>.</address></td>
  </tr>
</table>
</body>
</html>
