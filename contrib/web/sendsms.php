<html>
<font size=-2>
<? 

  $kannel = array ( host => "localhost", port => "13013", user => "user", pass => "pass");

  $string="";
  while(list($k,$v) = each($_GET)) {
	if ( $v != "" ) {
  	  $fields[$k]= "$v";
	}
  }
  while(list($k,$v) = each($_POST)) {
	if ( $v != "" ) {
  	  $fields[$k]= "$v";
	}
  }
  if ( $fields['debug'] + 0 != "0" ) {
    $debug = 1;
  } else $debug=0;
  $fields['debug'] = "";

  if ( $fields['text'] != "" ) {
    if ( ! preg_match("/%/", $fields['text'])) {
      $fields['text']  = urlencode($fields['text']);
    }
  }
  if ( $fields['image'] != "" ) $fields['image'] = preg_replace("/(..)/", "%$1", $fields['image']);
  if ( $fields['tune'] != "" ) $fields['tune'] = preg_replace("/(..)/", "%$1", $fields['tune']);



  if ( $mode == "LOGO") {
    if ($fields['country'] == "" ) $fields['country']="268";
    if ($fields['operator'] == "" ) $fields['operator']="01";

    $fields['image'] = substr($fields['image'], 0, 72*14*3/8);
    $fields['udh'] = "%06%05%04%15%82%00%00";
    $fields['text'] = "%" .substr($fields['country'],1,1) .substr($fields['country'],0,1) ."%F" . substr($fields['country'],2,1) .    
      "%". substr($fields['operator'],1,1) . substr($fields['operator'],0,1) . 
      "%00%48%0E%01".$fields['image'];
    $fields['image'] = "";
    $fields['country'] = "";
    $fields['operator'] = "";
    sendsms();
  }

  else if ( $mode == "VCARD") {
    $fields['udh'] = "%06%05%04%23%F4%00%00";
    $fields['vcard'] = "";
    
    $fields['text'] = "BEGIN:VCARD%0D%0AVERSION:2.1%0D%0A";
    if ( $fields['name'] != "") { $fields['text'] .= "N:".urlencode($fields['name']) . "%0D%0A"; $fields['name']=""; }
    if ( $fields['tele_pref'] != "") { $fields['text'] .= "TEL;PREF:".urlencode($fields['tele_pref']) . "%0D%0A"; $fields['tele_pref']=""; }
    if ( $fields['tele_cell'] != "") { $fields['text'] .= "TEL;CELL:".urlencode($fields['tele_cell']) . "%0D%0A"; $fields['tele_cell']=""; }
    if ( $fields['tele_home'] != "") { $fields['text'] .= "TEL;HOME:".urlencode($fields['tele_home']) . "%0D%0A"; $fields['tele_home']=""; }
    if ( $fields['tele_work'] != "") { $fields['text'] .= "TEL;WORK:".urlencode($fields['tele_work']) . "%0D%0A"; $fields['tele_work']=""; }
    if ( $fields['tele_fax'] != "") { $fields['text'] .= "TEL:FAX:".urlencode($fields['tele_fax']) . "%0D%0A"; $fields['tele_fax']=""; }
    if ( $fields['email'] != "") { $fields['text'] .= "EMAIL:".urlencode($fields['email']) . "%0D%0A"; $fields['email']=""; }
    if ( $fields['label'] != "") { $fields['text'] .= "LABEL:".urlencode($fields['label']) . "%0D%0A"; $fields['label']=""; }
    if ( $fields['note'] != "") { $fields['text'] .= "NOTE:".urlencode($fields['note']) . "%0D%0A"; $fields['note']=""; }
    $fields['text'] .= "END:VCARD%0D%0A";
    sendsms();
  }

  else if ( $mode == "VCAL") {
    $fields['udh'] = "%06%05%04%23%F5%00%00";
    $fields['vcal'] = "";
    
    $fields['text'] = "BEGIN:VCALENDAR%0D%0AVERSION:1.0%0D%0ABEGIN:VEVENT%0D%0A";
    if ( $fields['vcal_name'] != "") { $fields['text'] .= "DESCRIPTION:".urlencode($fields['vcal_name']) . "%0D%0ASUMMARY:".urlencode($fields['vcal_name'])."%0D%0A"; $fields['vcal_name']=""; }
    if ( $fields['date'] != "") { $fields['text'] .= "DTSTART:".urlencode($fields['date']) . "%0D%0A"; $fields['date']=""; }
    if ( $fields['categories'] != "") { $fields['text'] .= "CATEGORIES:".urlencode($fields['categories']) . "%0D%0A"; $fields['categories']=""; }
    if ( $fields['rule'] != "") { $fields['text'] .= "RULES:".urlencode($fields['rule']) . "%0D%0A"; $fields['rule']=""; }
    if ( $fields['alarm'] != "") { $fields['text'] .= "DALARM:".urlencode($fields['alarm']) . ";;;%0D%0AAALARM:".urlencode($fields['alarm']).";;;%0D%0A"; $fields['alarm']=""; }
    $fields['text'] .= "END:VEVENT%0D%0AEND:VCALENDAR%0D%0A";
    sendsms();
  }

  else if ( $mode == "GROUP") {
    $fields['image'] = substr($fields['image'], 0, 72*14*3/8);
    $fields['udh'] = "%06%05%04%15%83%00%00";
    $fields['text'] = "%00%48%0E%01".$fields['image'];
    $fields['image'] = "";
    sendsms();
  }

  else if ( $mode == "TUNE") {
    $fields['udh'] = "%06%05%04%15%81%00%00";
    $fields['text'] = $fields['tune'];
    $fields['tune'] = "";
    sendsms();
  }

  else if ( $mode == "PICTURE") {
  print "PICTURE<br>";
    $fields['image'] = substr($fields['image'], 0, 72*$fields['imagesize']*3/8);
    $fields['udh'] = "%06%05%04%15%8A%00%00";
    $fields['text'] = "%30%00" . "%00". "%" . sprintf("%02X", strlen($fields['text'])).$fields['text']. "%02" . ($fields['imagesize'] == "14" ? "%00%82" : "%01%00") . "%00%48%".($fields['imagesize'] == "14" ? "0E" : "1C"). "%01".$fields['image'];
    $fields['image'] = "";
    $fields['imagesize'] = "";
    sendsms();
  }

  else if ( $mode == "MWI") {
    if ( $fields['mwi'] > 4) {
    	$fields['text'] = "";
    	$fields['charset'] = "";
    	$fields['mwi_messages'] = "";
    	$fields['coding'] = "";
    }
    if ( $fields['mwi_messages'] != "" ) {
    	$fields['udh'] = "%04%01%02%".($fields['text'] == "" ? '0' : 'C'). ($mwi-1). "%". sprintf("%02X", $fields['mwi_messages']);
	if($fields['coding'] == 0) { $fields['coding'] = 1; }
        $fields['mwi_messages'] = "";
    }
    sendsms();
  }

  else if ( $mode == "BOOKMARK") {
    $fields['udh'] = "%06%05%04%C3%4F%00%00";
    $fields['name'] = urlencode($fields['name']);
    $fields['url'] = urlencode($fields['url']);
    $fields['text'] = "%01%06%2D%1F%2B%61%70%70%6C%69%63%61%74%69%6F%6E%2F%78%2D%77%61%70%2D%70%72%6F%76%2E%62%72%6F%77%73%65%72%2D%62%6F%6F%6B%6D%61%72%6B%73%00%81%EA%00%01%00%45%C6%7F%01%87%15%11%03". $fields['name']. "%00%01%87%17%11%03". $fields['url']. "%00%01%01%01";
    $fields['name'] = "";
    $fields['url'] = "";
    sendsms();
  }

  else if ( $mode == "WAPCONFIG") {
    $fields['udh'] = "%06%05%04%C3%4F%00%00";

    $fields['name'] = urlencode($fields['name']);
    $fields['url'] = urlencode($fields['url']);

    $fields['text'] = "";
    $fields['text'] .= "%01"; # Transaction ID / Push ID
    $fields['text'] .= "%06"; # PDU Type (Push)
    $fields['text'] .= "%2C"; # Headers Lenght (content-type + headers)
    $fields['text'] .= "%1F"; # ? Length
    $fields['text'] .= "%2A" . urlencode("application/x-wap-prov.browser-settings") . "%00" ; # Content-Type
    $fields['text'] .= "%81%EA"; # charset = UTF-8

    $fields['text'] .= "%01"; # Version WBXML 1.1
    $fields['text'] .= "%01"; # Unknown Public Identifier
    $fields['text'] .= "%6A"; # Charset UTF-8
    $fields['text'] .= "%00"; # String table length


    $params = array ( "bearer" => "12", "proxy" => "13", "port" => "14", "name" => "15", "proxy_type" => "16", "url" => "17", "proxy_authname" => "18", "proxy_authsecret" => "19", "sms_smsc_address" => "1A", "ussd_service_code" => "1B", "gprs_accesspointname" => "1C", "ppp_logintype" => "1D", "proxy_logintype" => "1E", "csd_dialstring" => "21", "csd_calltype" => "28", "csd_callspeed" => "29", "ppp_authtype" => "22", "ppp_authname" => "23", "ppp_authsecret" => "24" );
    $params_with_attr = array ( "bearer", "port", "proxy_type", "ppp_logintype", "proxy_logintype", "csd_calltype", "csd_callspeed", "ppp_authtype");

    #$otadebug=1;

    $fields['text'] .= "%45"; # <CHARACTERISTIC_LIST>

      if ( $fields['name'] != "" ) {
        $fields['text'] .= "%C6%08%01"; # <CHARACTERISTIC TYPE="NAME">
        $fields['text'] .= "%87%15%11"; # <PARM NAME="NAME" VALUE=...
        $fields['text'] .= "%03" . $fields['name'] ."%00"; # ..."$name"...
        $fields['text'] .= "%01"; # .../>
        $fields['text'] .= "%01"; # </CHARACTERISTIC>
      }
      
      if ( $fields['url'] != "" ) {
        $fields['text'] .= "%86%07%11"; # <CHARACTERISTIC TYPE="URL" VALUE=...
        $fields['text'] .= "%03" . plusencode($fields['url']) ."%00"; # ..."$name"...
        $fields['text'] .= "%01"; # </CHARACTERISTIC>
      }

      if ($fields['name'] != "" && $fields['url'] != "") {
        $fields['text'] .= "%C6%7F%01"; # <CHARACTERISTIC TYPE="BOOKMARK">
        $fields['text'] .= "%87%15%11"; # <PARM NAME="NAME" VALUE=...
        $fields['text'] .= "%03" . $fields['name'] ."%00"; # ..."$name"...
        $fields['text'] .= "%01"; # .../>
        $fields['text'] .= "%87%17%11"; # <PARM NAME="URL" VALUE=...
        $fields['text'] .= "%03" . $fields['url'] ."%00"; # ..."$name"...
        $fields['text'] .= "%01"; # .../>
        $fields['text'] .= "%01"; # .../>
      }

      $fields['name'] = "";
      $fields['url'] = "";

      $fields['text'] .= "%C6%06%01"; # <CHARACTERISTIC TYPE="ADDRESS">
      while(list($key, $val) = each($params)) {
      	if ( $fields[$key] != "" && $fields[$key] != "0" ) { 

  	  if ($otadebug == 1) { $fields['text'].="&lt;parm name=$key ($val)&gt;"; }
      	  $fields['text'] .= "%87"; # <PARM> with attributes
      	  $fields['text'] .= "%".$val; # NAME=
	  if ( in_array($key, $params_with_attr) ) {
	    if ($otadebug == 1) { $fields['text'].="&lt;value key=$key val=".$fields[$key]."&gt;"; }
      	    $fields['text'] .= "%" . $fields[$key]; # VALUE=CODE
	  } else {
	    if ($otadebug == 1) { $fields['text'].="&lt;value key=$key val=".$fields[$key]."&gt;"; }
      	    $fields['text'] .= "%11"; # VALUE=
      	    $fields['text'] .= "%03". plusencode($fields[$key]) . "%00"; # STRING
	  }
      	  $fields['text'] .= "%01"; # </PARM>

	}
	$fields[$key] = "";
      }
      $fields['text'] .= "%01"; # </CHARACTERISTIC>

    $fields['text'] .= "%01"; # </CHARACTERISTIC_LIST>
    sendsms();
  }


  else if ( $mode == "WAPPUSHSI") {

    $fields['text'] = "";

    $fields['udh'] = "%06%05%04%0B%84%23%F0";

    $MIME=urlencode("application/vnd.wap.sic");
    $fields['text'] .= "%01";   # Transaction ID
    $fields['text'] .= "%06";  # PDU Type (push)
    $fields['text'] .= "%04";        # Headers Length (content-type + headers)
    $fields['text'] .= "%03";        # Length of content type
    $fields['text'] .= "%AE";      # Content-Type: application/vnd.wap.sic
    $fields['text'] .= "%81";      # Charset
    $fields['text'] .= "%EA";      # UTF-8
    # End Headers
    
    # see si_binary_output
    $fields['text'] .= "%02";  # Version number (wbxml_version)
    $fields['text'] .= "%05";  # Unknown Public Identifier (si_public_id)
    $fields['text'] .= "%6A";  # charset= (sibxml->charset)
    $fields['text'] .= "%00";  # String table length

    $fields['text'] .= "%45"; # <si>
      $fields['text'] .= "%C6"; # <indication...
	$fields['text'] .= "%0b" . "%03" . $fields['url'] . "%00";	# href=$url
	$fields['text'] .= "%11" . "%03" . rand(1,9)."@vodafone.pt" . "%00";	# si-id=
	$fields['text'] .= "%08"; # action="signal-high"
	#$fields['text'] .= "%0A" . "%C3%07%20%01%10%21%20%02%23"; # created=
	#$fields['text'] .= "%10" . "%C3%04%20%02%06%30"; # valid=
        $fields['text'] .= "%01"; # end indication params 
	$fields['text'] .= "%03" . urlencode($fields['name']). "%00";
      $fields['text'] .= "%01"; # </indication>
    $fields['text'] .= "%01"; # </si>

    $fields['name']="";
    $fields['url']="";
    sendsms();
  }


  else if ( $mode == "WAPPUSHSL") {

    $fields['text'] = "";

    $fields['udh'] = "%06%05%04%0B%84%23%F0";

    $MIME=urlencode("application/vnd.wap.slc");
    #$fields['text'] .= "%01";   # Transaction ID
    #$fields['text'] .= "%06";  # PDU Type (push)
    #$fields['text'] .= "%04";        # Headers Length (content-type + headers)
    #$fields['text'] .= "%03";       # length of content type
    #$fields['text'] .= "%B0";      # Content-Type: application/vnd.wap.slc
    #$fields['text'] .= "%81";      # Charset
    #$fields['text'] .= "%EA";      # UTF-8

    $fields['text'] .= "%01";   # Transaction ID
    $fields['text'] .= "%06";  # PDU Type (push)
    $fields['text'] .= "%1B";        # Headers Length (content-type + headers)
    $fields['text'] .= "%1A";       # length of content type
    $fields['text'] .= $MIME."%00";      # Content-Type: application/vnd.wap.slc
    $fields['text'] .= "%81";      # Charset
    $fields['text'] .= "%EA";      # UTF-8
    # End Headers
    
    # see si_binary_output
    $fields['text'] .= "%02";  # Version number (wbxml_version)
    $fields['text'] .= "%06";  # Unknown Public Identifier (si_public_id)
    $fields['text'] .= "%6A";  # charset= (sibxml->charset)
    $fields['text'] .= "%00";  # String table length

    $fields['text'] .= "%85"; # <sl>
      $fields['text'] .= "%0b"; # action="signal-high"
      $fields['text'] .= "%08" . "%03" . $fields['url'] . "%00";	# href=$url
    $fields['text'] .= "%01"; # </si>

    $fields['name']="";
    $fields['url']="";
    sendsms();
  }

  else if ( $mode == "SIEMENS") {
    $fields['coding'] = "1";

    #print_r($mmc_file_raw);exit;
    #move_uploaded_file($mmc_file_raw, "/tmp/siemens.tmp");

    $h=fopen($mmc_file_raw, "rb");
    $file = fread($h, filesize($mmc_file_raw));
    fclose($h);
    $file_size = strlen($file);
    #$file = join("", file($mmc_file_raw));


    #print strlen($file); exit;
    $file = join("", unpack("H*", $file));
    $file = preg_replace("/(..)/", "%$1", $file);

    $max_size = 140 - 22 - 3 - strlen($mmc_file_name);

    $file_type = $fields['mmc_file_type'];
    $file_name = $fields['mmc_file_name'];
    $fields['mmc_file'] = "";
    $fields['mmc_file_type'] = "";
    $fields['mmc_file_name'] = "";

    $count = 0; 
    $max = ceil( $file_size / $max_size);
    $packet_size = $max_size;
    $object_size = sprintf("%08X", $file_size);
    $object_size = "%". substr($object_size, 6, 2).  "%". substr($object_size, 4, 2).  "%". substr($object_size, 2, 2).  "%". substr($object_size, 0, 2);

    print "max=$max, packetsize=$packet_size, objectsize=$object_size; file_size: ".$file_size."<p>";
    while ($count < $max) {

	    if ( $count == $max - 1 ) { # Last Packet
	    	$packet_size = $file_size % $max_size;
	    }

    
	    $fields['text'] = "";
	    $fields['text'] .= "//SEO";
	    $fields['text'] .= "%01"; # Version 1
	    $fields['text'] .= "%". sprintf("%02X", $packet_size % 256). "%". sprintf("%02X", floor($packet_size / 256)); # Data Size on this message
	    $fields['text'] .= "%00%00%00%00"; # Reference
	    $fields['text'] .= "%". sprintf("%02X", ($count+1) % 256). "%". sprintf("%02X", floor(($count+1) / 256)); # Packet Number
	    $fields['text'] .= "%". sprintf("%02X", $max % 256). "%". sprintf("%02X", floor($max / 256)); # Max Packets
	    $fields['text'] .= $object_size; # Object Size
	    $fields['text'] .= "%". sprintf("%02X", strlen($mmc_file_type)). $mmc_file_type; # Object Type
	    $fields['text'] .= "%". sprintf("%02X", strlen($mmc_file_name)). $mmc_file_name; # Object File Name

	    $fields['text'] .= substr($file, $count * $max_size * 3, $packet_size * 3);
	    #print $fields['text']. "<br>";
	    #$debug = 1;
	    sendsms();
	    $count++;
    }
  }

  else {
  	sendsms();
  }



  function sendsms (){
  	global $fields, $debug, $kannel;

	  $fields['mode'] = "";
	  reset($fields);
	  while(list($k,$v) = each($fields)) {
	    if ( $v != "" ) {
	      $string .= "&$k=$v";
	    }
	  }
	    
	  print ($debug ? "[DEBUG]" : "" )."Getting $string<br>";
	  if ( !$debug ) { $result = @file("http://".$kannel['host'].":".$kannel['port']."/sendsms?user=".$kannel['user']."&pass=".$kannel['pass']."&".$string); }
	  print_r( $result);
  }

  function plusencode($string) {
	return preg_replace("/\+/", "%2B", $string);
  }

?>
