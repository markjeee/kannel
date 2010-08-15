<html>
<head>
<title>Kannel Administration</title>
</head>
<body bgcolor="#FFFFFF" text="#000000">

<?php
include("config.inc");
include("functions.inc");

switch($command)
{
 case "status":
	$URL = "/cgi-bin/status";
 	http_send($URL,ADMIN_PORT);
 	break;
 case "suspend":
	$URL = "/cgi-bin/suspend?password=".ADMIN_PASSWORD;
 	http_send($URL,ADMIN_PORT);
 	break;
 case "isolate":
	$URL = "/cgi-bin/isolate?password=".ADMIN_PASSWORD;
 	http_send($URL,ADMIN_PORT);
 	break;
 case "resume":
	$URL = "/cgi-bin/resume?password=".ADMIN_PASSWORD;
 	http_send($URL,ADMIN_PORT);
 	break;
 case "shutdown":
	$URL = "/cgi-bin/shutdown?password=".ADMIN_PASSWORD;
 	http_send($URL,ADMIN_PORT);
 	break;
 default:
	echo "command $command not yet implemented<br>\n";
}
 echo "<p><address><a href=\"$PHP_SELF?command=$command\">Reload page</a></address></p>\n";
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
