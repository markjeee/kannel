<html> 
<body bgcolor=white text=black link=blue vlink=navy alink=red>

<script>
   function help(texto) {
     document.all.help.innerHTML = texto; 
   }

</script>
<base target="envia">
<?php

	function help($fields) {
		return "<h1>Help</h1><h2>".$fields[0]."</h2><p><i>".$fields[2]."</i></p>";
	}

	$aModes = array ( "TEXT", "LOGO", "GROUP", "PICTURE", "TUNE", "PROFILE", 
		   "VCARD", "VCAL", "MWI", "WAPCONFIG", "BOOKMARK", "RAW");

	$aFields = array (
		"from" => array ( "From",
			"<input size=16 name=from>", 
			"Numeric (16chars) or alphanumeric (11chars)",
		),
		"to" => array ( "To",
			"<input size=16 name=to>", 
			"Numeric (16 chars)",
		),
		"text" => array ( "Text",
			"<textarea name=text rows=4 cols=40></textarea>", 
			"Text message (for UNICODE messages, see <i>charset</i>",
		),
		"charset" => array ( "Charset", 
			"<input size=16 name=charset>", 
			"(Optional) Charset (ex: ISO-8859-1, UTF-16BE, UTF-8, etc). Defaults to ISO-8859-1 or UTF-16BE with UNICODE",
		),
		"udh" => array ( "User Data Header", 
			"<textarea name=udh rows=2 cols=40></textarea>", 
			"Encode as 001122AAFF",
		),
		"mclass" => array ( "Message Class", 
			'<select name="mclass"> <option value="0">Undefined</option> <option value="1">Visor</option> <option value="2">Mobile</option> <option value="3">SIM</option> <option value="4">SIM Toolkit</option> </select>', 
			"aaa",
		),
		"coding" => array ( "Data Coding", 
			'<select name="coding"> <option value="0">Undefined</option> <option value="0">Texto</option> <option value="2">UNICODE</option> </select>', 
			"A opção <i>Texto</i> é o normal, onde o texto é convertido para o alfabeto GSM e cada mensagem leva no máximo 160 caracteres.<br>No caso de UNICODE, a mensagem vai até 70 caracteres mas o texto não é convertido e suporta todo o alfabeto UNICODE, desde que o telefone também suporte",
		),
		"mwi" => array ( "Message Waiting Indicator", 
			'<select name="mwi"> <option value="0">Voice Mail, active</option> <option value="1">Fax, active</option> <option value="2">E-Mail, active</option> <option value="3">Other, active</option> <option value="4">Voice Mail, inactive</option> <option value="5">Fax, inactive</option> <option value="6">E-Mail, inactive</option> <option value="7">Other, inactive</option> </select>', 
			"aaa",
		),
		"mwi_messages" => array ( "Messages", 
			"<input size=2 name=mwi_messages>", 
			"Number of messages",
		),
		"validity" => array ( "Validity", 
			"<input size=8 name=validity>", 
			"Message will be retried on SMSC for this many minutes",
		),
		"deferred" => array ( "Deferred", 
			"<input size=8 name=deferred>", 
			"Message will be deferred on SMSC for this many minutes",
		),
		"country" => array ( "Country", 
			"<input size=8 name=country>", 
			"Contry code",
		),
		"operator" => array ( "Operator", 
			"<input size=8 name=operator>", 
			"Operator Code",
		),
		"image" => array ( "Image", 
			'Hex Image:  <table><tr><td><table background="Nokia.jpg" width="151" height="143"> <tr><td align=center> <applet code=GSMViewer.class name=GSMViewer width=72 height=14 VIEWASTEXT> <param name=ActiveColor value="000000"> <param name=InactiveColor value="7FBF6F"> <param name=BackColor value="7FBF6F"> <param name=Rows value=28> <param name=Cols value=72> </applet> </td></tr></table> </td>'.  '<td><applet code="GSM.class" name=GSM width=362 height=72  VIEWASTEXT> <param name=ActiveColor value="000000"> <param name=InactiveColor value="7FBF6F"> <param name=BorderColor value="808080"> <param name=BackColor value="7FBF6F"> <param name=Rows value=28> <param name=Cols value=72> <param name=CellHeight value=5> <param name=Viewer value ="GSMViewer"> </applet> <br>'.  ' <input type="button" onclick="GSM.FClear()" value="Ini"> <input type="button" onclick="GSM.FInvert()" value="Inv"> <input type="button" onclick="GSM.FHorizontalFlip()" value="H"> <input type="button" on click="GSM.FVerticalFlip();" value="V"> <input type="button" onclick="GSM.FUp()" value="/\"> <input type="button" onclick="GSM.FDown()" value="\/"> <input type="button" onclick="GSM.FLeft()" value="<"> <input type="button" onclick="GSM.FRight()" value=">"> </td></tr></table> <script> function update_java() { ; heximage = send.image.value; binimage=""; for(i=0;i<504;i++) { ; x="1"+heximage.substr(i,1); y=parseInt(x,16); z=y.toString(2); z=z.substr(1,4); binimage +=z; }; GSM.FSetValues(binimage); } ; function update_image() { ;  nokiaimage = GSM.FGetValues(); heximage=""; for(i=0;i<504;i++) { ; x=nokiaimage.substr(i*4, 4); y=parseInt(x,2); heximage += y.toString(16); }; send.image.value = heximage; } </script><input type="text" name=image onChange="update_java();"><br><input type=submit value="Update Hex Image field" onClick="update_image(); return false;";>',
			"Enviar encoded 001122AAFF",
			'update_image();',
		),
		"imagesize" => array ( "Image Size",
			'<script> function change_image_size() { GSM.height = send.imagesize.value * 5 + 2; GSMViewer.height = send.imagesize.value; GSM.Rows = send.imagesize.value; GSMViewer.Rows = send.imagesize.value; } </script><select name="imagesize" onChange="change_image_size();"><option value=14>14</option><option value=28>28</option></select>',
			'hexencoded tone',
		),
		"tune" => array ( "Ringing Tone",
			'<input type=text name="tune"><br><APPLET CODE="composer.RingRing.class" WIDTH="300" HEIGHT="40" ARCHIVE="ringring/classes.zip" MAYSCRIPT> </APPLET>',
			'hexencoded tone',
		),
		"name" => array ( "Name",
			'<input type=text name="name">',
			'Nome URL',
		),
		"url" => array ( "URL",
			'<input type=text name="url">',
			'URL',
		),
		"urlpush" => array ( "URL",
			'<input type=text name="url">',
			'URL; wtai://wp/mc;phone, wtai://wp/ap;phone;name',
		),
		"bearer" => array ("Wap Bearer", 
			'<select name=bearer><option value=45>GSM/CSD<option value=46>GSM/SMS<option value=47>GSM/USSD<option value=48>IS-i36/CSD<option value=49>GPRS</select>',
			'Bearer to connect to wap',
		),
		"ppp_authtype" => array ("Authentication Type <br><font size=-3>GSM/CSD, IS-I36/CSD, GPRS</font>", 
			'<select name=ppp_authtype><option value=0>Select...<option value=70>PAP<option value=71>CHAP<option value=78>MS_CHAP</select>',
			'PPP Authentication Type. GSM/CSD, IS-i36/CSD and GPRS',
		),
		"ppp_authname" => array ("PPP Authentication Name (username)<br><font size=-3>GSM/CSD, IS-I36/CSD, GPRS</font>", 
			'<input size=32 maxlength=32 type=text name=ppp_authname>',
			'PPP Authentication Name. GSM/CSD, IS-i36/CSD and GPRS',
		),
		"ppp_authsecret" => array ("PPP Authentication Secret (password)<br><font size=-3>GSM/CSD, IS-I36/CSD, GPRS</font>", 
			'<input size=20 maxlength=20 type=text name=ppp_authsecret>',
			'PPP Authentication Secret. GSM/CSD, IS-i36/CSD and GPRS',
		),
		"ppp_logintype" => array ("PPP Login Type<br><font size=-3>GSM/CSD, IS-I36/CSD, GPRS</font>", 
			'<select name=ppp_logintype><option value=0>Select...<option value=64>Automatic<option value=65>Manual</select>',
			'PPP Login Type. GSM/CSD, IS-i36/CSD and GPRS',
		),
		"proxy" => array ("Proxy<br><font size=-3>Required</font>", 
			'<input size=21 maxlength=21 type=text name=proxy>',
			'Required. GSM/CSD (wap gateway), GSM/SMS (SMSC MSISDN), GSM/USSD (IP or MSISDN), IS-i36/CSD and GPRS',
		),
		"proxy_type" => array ("Proxy Type<br><font size=-3>GSM/USSD</font>", 
			'<select name=proxy_type><option value=0>Select...<option value=76>MSISDN (default)<option value=77>IPV4</select>',
			'Proxy Type. GSM/USSD, required.',
		),
		"proxy_authname" => array ("Proxy Auth Name<br><font size=-3>GSM/CSD, IS-I36/CSD, GPRS</font>", 
			'<input size=32 maxlength=32 type=text name=proxy_authname>',
			'Proxy Auth Name. GSM/CSD, IS-i36/CSD, GPRS.',
		),
		"proxy_authsecret" => array ("Proxy Auth Secret<br><font size=-3>GSM/CSD, IS-I36/CSD, GPRS</font>", 
			'<input size=20 maxlength=20 type=text name=proxy_authsecret>',
			'Proxy Auth Secret. GSM/CSD, IS-i36/CSD, GPRS.',
		),
		"proxy_logintype" => array ("Proxy Login Type<br><font size=-3>GSM/CSD, IS-I36/CSD, GPRS</font>", 
			'<select name=ppp_logintype><option value=0>Select...<option value=64>Automatic<option value=65>Manual</select>',
			'Proxy Login Type. GSM/CSD, IS-i36/CSD, GPRS.',
		),
		"port" => array ("Wap Port", 
			'<select name=port><option value=0>Select...<option value=60>9200<option value=61>9201<option value=62>9202<option value=63>9203</select>',
			'Wap Port. 9200 (connectionless), 9201 (connection oriented) or 9202/9203 (wtls equivalents). All.',
		),
		"csd_dialstring" => array ("CSD Dialstring<br><font size=-3>Required; GSM/CSD, IS-I36/CSD</font>", 
			'<input size=21 maxlength=21 type=text name=csd_dialstring>',
			'CSD Dial string. GSM/CSD (required), IS-i36/CSD (required)',
		),
		"csd_calltype" => array ("CSD Call Type<br><font size=-3>GSM/CSD</font>", 
			'<select name=csd_calltype><option value=0>Select...<option value=72>Analogue<option value=73>ISDN</select>',
			'CSD Call Type. GSM/CSD',
		),
		"csd_callspeed" => array ("CSD Call Speed<br><font size=-3>GSM/CSD</font>", 
			'<select name=csd_callspeed><option value=0>Select...<option value="6A">AUTO<option value=6B>9600<option value="6C">14400<option value="6D">19200<option value="6E">28800<option value="6F">38400<option value="74">43200<option value="75">57600</select>',
			'CSD Call Type. GSM/CSD',
		),
		"isp_name" => array ("ISP Name<br><font size=-3>GSM/CSD, IS-I36/CSD, GPRS</font>", 
			'<input size=20 maxlength=20 type=text name=isp_name>',
			'ISP Name.',
		),
		"sms_smsc_address" => array ("SMSC Address<br><font size=-3>Required; GSM/SMS</font>", 
			'<input size=21 maxlength=21 type=text name=sms_smsc_address>',
			'SMSC Address.',
		),
		"ussd_service_code" => array ("USSD Service Code<br><font size=-3>Required; USSD</font>", 
			'<input size=10 maxlength=10 type=text name=ussd_service_code>',
			'USSD Service Code.',
		),
		"gprs_accesspointname" => array ("GPRS Access Point Name<br><font size=-3>Required; GPRS</font>", 
			'<input size=32 maxlength=100 type=text name=gprs_accesspointname>',
			'GPRS Access Point Name. GPRS (required)',
		),
		"mmc_file_type" => array ("File Type", 
			'<select name=mmc_file_type><option value=0>Select...<option value="mid">Midi<option value="bmp">Bitmap<option value="lng">Language<option value="mp3">Mp3<option value="jar">Jar<option value="jad">Jad</select>',
			'File Type',
		),
		"mmc_file_name" => array ("File Name", 
			'<input type=text name=mmc_file_name>',
			'File Name (use "path/filename" for example with jad/jar files',
		),
		"mmc_file_raw" => array ("File", 
			'<input type=file name=mmc_file_raw>',
			'File',
		),
		"tele_pref" => array ("Telefone (Predefinido)", 
			'<input type=text name=tele_pref>',
			'Default phone',
		),
		"tele_cell" => array ("Telemóvel", 
			'<input type=text name=tele_cell>',
			'Cell phone',
		),
		"tele_home" => array ("Telefone Casa", 
			'<input type=text name=tele_home>',
			'Home phone',
		),
		"tele_work" => array ("Telefone Trabalho", 
			'<input type=text name=tele_work>',
			'Work phone',
		),
		"tele_fax" => array ("Fax", 
			'<input type=text name=tele_fax>',
			'Fax',
		),
		"email" => array ("Email", 
			'<input type=text name=email>',
			'email',
		),
		"label" => array ("Morada", 
			'<input type=text name=label>',
			'Address',
		),
		"note" => array ("Nota", 
			'<input type=text name=note>',
			'Note',
		),
		"vcard" => array ("VCard", 
			'<textarea rows=8 name=vcard></textarea><input type=submit value="Update" onClick=\'vcard.value="BEGIN: VCARD\nVERSION:2.1\n"; if(send.name.value != "") { vcard.value += "N: "+send.name.value+"\n"; } ; if(send.tele_pref.value != "") { vcard.value += "TEL;PREF: "+send.tele_pref.value+"\n"; } ; if(send.tele_cell.value != "") { vcard.value += "TEL;CELL: "+send.tele_cell.value+"\n"; } ; if(send.tele_home.value != "") { vcard.value += "TEL;HOME: "+send.tele_home.value+"\n"; } ; if(send.tele_work.value != "") { vcard.value += "TEL;WORK: "+send.tele_work.value+"\n"; } ; if(send.tele_fax.value != "") { vcard.value += "TEL;FAX: "+send.tele_fax.value+"\n"; } ; if(send.email.value != "") { vcard.value += "EMAIL: "+send.email.value+"\n"; } ; if(send.label.value != "") { vcard.value += "LABEL: "+send.label.value+"\n"; } ; if(send.note.value != "") { vcard.value += "NOTE: "+send.note.value+"\n"; } ; vcard.value +="END:VCARD"; return false;\'>',
			'Vcard',
		),
		"date" => array ("Date (yyyymmddThhmmss)", 
			'<input type=text name=date>',
			'Date',
		),
		"vcal_name" => array ("Name", 
			'<input type=text name=vcal_name>',
			'Examples: <br>Birthday:<br>Name: YYYY Name<br>Categories: Birthday<br>Rule: YD1 #0<br><br>Call:<br>Name: phone name<br><br>Weekly meeting:<br>Rule: W1 #0',
		),
		"categories" => array ("Categories", 
			'<select name=categories><option value="">Select...<option value="SPECIAL OCCASION">Birthday<option value="PHONE CALL">Call<option value="MEETING">Meeting</select>',
			'Categories',
		),
		"rule" => array ("Rule", 
			'<input type=text name=rule>',
			'Examples:<br>Repeat week: "W1 #0"<br>Repeat daily: "D1 #0"<br>Repeat yearly: "YD1 #0"',
		),
		"alarm" => array ("Alarm (yyyymmddThhmmss)", 
			'<input type=text name=alarm>',
			'Date',
		),
		"vcal" => array ("VCal", 
			'<textarea rows=8 name=vcal></textarea><input type=submit value="Update" onClick=\'vcal.value="BEGIN: VCALENDAR\nVERSION:1.0\nBEGIN:VEVENT\n"; if(send.vcal_name.value != "") { vcal.value += "DESCRIPTION: "+send.vcal_name.value+"\nSUMMARY: "+send.vcal_name.value+"\n"; } ; if(send.date.value != "") { vcal.value += "DTSTART: "+send.date.value+"\n"; } ; if(send.categories.value != "") { vcal.value += "CATEGORIES: "+send.categories.value+"\n"; } ; if(send.rule.value != "") { vcal.value += "RULES: "+send.rule.value+"\n"; } ; if(send.alarm.value != "") { vcal.value += "DALARM: "+send.alarm.value+"\nAALARM: "+send.alarm.value+"\n"; } ; vcal.value +="END:VEVENT\nEND:VCARD"; return false;\'>',
			'VCalendar',
		),
		"incompleto" => array ( "INCOMPLETO",
			'<h2>Este formulario ainda está incompleto</h2>',
			'URL',
		),
	);
?>
<?php

	$aModeFields = array (
		"ALL" => array ("from", "to", "validity", "deferred"),
		"TEXT" => array ("text", "charset", "mclass", "coding"),
		"LOGO" => array ("country", "operator", "image"),
		"GROUP" => array ("image"),
		"PICTURE" => array ("image", "imagesize", "text"),
		"TUNE" => array ("tune"),
		"PROFILE" => array ("image","text", "incompleto"),
		"VCARD" => array ("name", "tele_pref", "tele_cell", "tele_home", "tele_work", "tele_fax", "email", "label", "note", "vcard"),
		"VCAL" => array ("vcal_name", "date", "categories", "rule", "alarm", "vcal"),
		"MWI" => array ("mwi", "mwi_messages", "text", "charset", "coding"),
		"WAPCONFIG" => array ("name", "url", "bearer", "ppp_authtype", "ppp_authname", "ppp_authsecret", "ppp_logintype", "proxy", "proxy_type", "proxy_authname", "proxy_authsecret", "proxy_logintype", "port", "csd_dialstring", "csd_calltype", "csd_callspeed", "isp_name", "sms_smsc_address", "ussd_service_code", "gprs_accesspointname"),
		"BOOKMARK" => array ( "name", "url"),
		"WAPPUSHSI" => array ( "name", "urlpush"),
		"WAPPUSHSL" => array ( "url"),
		"SIEMENS" => array ( "mmc_file_type", "mmc_file_name", "mmc_file_raw"),
		"RAW" => array("udh", "incompleto"),
	);

?>

<h1>Sendsms <?=$mode?></h1>
<table width=100%>
<tr><td valign=top>


<form enctype="multipart/form-data" name=send method=POST action="sendsms.php">
<table>


<?php
   $submit_string="";
   while (list ($k, $field) = each ($aModeFields["ALL"])) {
   	$a = $aFields[$field];
	$submit_string .= $a[3];
?>   	
<tr><td>[<a href="" onClick='help("<?=help($a)?>"); return false;'>?</a>]<td><?=$a[0]?>:</td><td><?=$a[1]?></td></tr>
<?php
  }
?>
</table>
<hr width=90%>
<table>
<?php
   while (list ($k, $field) = each ($aModeFields[$mode])) {
   	$a = $aFields[$field];
	$submit_string .= $a[3];
?>   	
<tr><td>[<a href="" onClick='help("<?=escapeshellcmd(help($a))?>"); return false;'>?</a>]<td><?=$a[0]?>:</td><td><?=$a[1]?></td></tr>
<?php
  }
?>

</table>
<hr width=90%>
<input type=hidden name=mode value=<?=$mode?>>
<!--<input type=text name=debug value=1>-->
<input type=submit value="Enviar" onClick='<?=$submit_string?>'>
<input type=reset value="Limpar Valores">
</form>

</td><td width=25% valign=top><div id="help"><h1>Help</h1></div>
</td></tr></table>

</body>
</html>

