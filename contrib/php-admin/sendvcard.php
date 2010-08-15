<html>
<head>
<title>vCard SMS Sender</title>
</head>
<body bgcolor="#FFFFFF" text="#000000">

<?php
include("config.inc");
include("functions.inc");

if ($submit)
{
 $text  = "BEGIN:VCARD\r\n";
 $text .= "VERSION:2.1\r\n";
 $text .= "N:".$lastname.";".$firstname."\r\n";
 $text .= "TEL;PREF:+".$telephone."\r\n";
 $text .= "END:VCARD\r\n";
 echo "Sending the vCard SMS message <b>\"$text\"</b> to the phone <b>$to</b>...<br>\n";
 $URL = "/cgi-bin/sendsms?username=".USERNAME."&password=".PASSWORD."&from=".GLOBAL_SENDER."&to=$to&udh=%06%05%04%23%F4%00%00&text=".urlencode($text);
// echo "URL: $URL<br>\n";
 http_send($URL,SENDSMS_PORT);
 echo "<address><a href=\"$PHP_SELF\">Back to Send vCard SMS</a></address>\n";
} else {

?>

<h1>vCard SMS Sender</h1>
<form name="sendvcard" method="post" action="<?php echo "$PHP_SELF" ?>">
<p>
Telephone number to send the SMS to:
<br>
<input type="text" size="30" name="to">
</p>
<p>
vCard:
<br>
<table border=0>
<tr><td>First name</td><td><input type=text" size="30" name="firstname"> <i>(ie. John)</i></td></tr>
<tr><td>Last name</td><td><input type=text" size="30" name="lastname"> <i>(ie. Doe)</i></td></tr>
<tr><td>Telephone</td><td><input type=text" size="30" name="telephone"> <i>(ie. 4512345678)</i></td></tr>
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
