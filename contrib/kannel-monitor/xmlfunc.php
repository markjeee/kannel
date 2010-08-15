<?php

/*
 * xmlfunc.php -- Kannel's XML status output parsing functions.
 */

function nf($number) {
    return number_format($number, 0, ",", ".");
}

function nfd($number) {
    return number_format($number, 2, ",", ".");
}

function get_timeout() {
    $refresh = intval($_REQUEST['refresh']);
    return ($refresh > 0) ? $refresh : DEFAULT_REFRESH;
}

function get_uptime($sec) {
    $d = floor($sec/(24*3600));
    $sec -= ($d*24*3600);
    $h = floor($sec/3600);
    $sec -= ($h*3600);
    $m = floor($sec/60);
    $sec -= ($m*60);
    return sprintf("%dd %dh %dm %ds", $d, $h, $m, $sec);
} 

function count_smsc_status($smscs) {
    $stats = array(
        'online' => 0,
        'disconnected' => 0,
        'connecting' => 0,
        're-connecting' => 0,
        'dead' => 0,
        'unknown' => 0
    );
    foreach ($smscs as $smsc) {
        foreach ($stats as $st => $i) {
            if (substr($smsc['status'], 0, strlen($st)) == $st) {
                $stats[$st]++;
            }
        }
    }
    return $stats;
}

function get_smscids($status, $smscs) {
    /* loop the smsc */ 
    $n = "";
    foreach ($smscs as $smsc) {
        if (substr($smsc['status'], 0, strlen($status)) == $status) {
           $n .= $smsc['admin-id']." ";
        }
    }

    return $n;
}

function format_status($st) {
    $span = 'text';
    switch ($st) {
        case "online":
            $span = 'green';
            break;
        case "disconnected":
        case "connecting":
        case "re-connecting":
            $span = 'red';
            break;
    }
    return "<span class=\"$span\">$st</span>";
}

/*
 * Parse start date, uptime and status from the status text
 */
function parse_uptime($str) {
    $regs = array();
    if (ereg("(.*), uptime (.*)d (.*)h (.*)m (.*)s", $str, $regs) ||
        ereg("(.*) (.*)d (.*)h (.*)m (.*)s", $str, $regs)) {
        $ts = ($regs[2]*24*60*60) + ($regs[3]*60*60) + ($regs[4]*60) + $regs[5];
        $bb_time[$inst] = mktime()-$ts;
        $started = date("Y-m-d H:i:s", mktime()-$ts);
        $uptime = sprintf("%dd %02d:%02d:%02d", $regs[2], $regs[3], $regs[4], $regs[5]);
        $status = $regs[1];
    } else {
        $started = '-';
        $uptime = '-';
        $status = '-';
    }
    return array($status, $started, $uptime);
}

/*
 * Create a link for the SMSC status with a detail popup
 */
function make_link($smsc_status, $state, $mode='red') {
    global $status, $inst;
    if ($state == 'total') {
        return ($smsc_status > 0) ? "$smsc_status links":"none";
    } elseif ($smsc_status[$state] == 0) {
        return "none";
    } else {
        switch ($mode) {
            case 'red':
                return "<a href=\"#\" class=href onClick=\"do_alert('".
                       "smsc-ids in $state state are\\n\\n".
                       get_smscids($state, $status[$inst]['smscs']).
                       "');\"><span class=red><b>".
                       $smsc_status[$state].
                       "</b> links</span></a>";
                break;
            case 'green':
                return "<span class=green><b>".
                       $smsc_status[$state].
                       "</b> links</span></a>";
                break;
        }
    }
}

/*
 * Split the load text into 3 <TD>
 */
function split_load($str) {
    if (!$str) {
        return "<td>-</td><td>-</td><td>-</td>\n";
    } else {
        return "<td>".implode("</td><td>", explode(",", $str))."</td>\n";
    }
}

/*
 * Create the admin link to change bearerbox status
 */
function admin_link($mode) {
    global $config;
    return "<a class=\"href\" href=\"#\" onClick=\"admin_url('".$mode."', ".
         "'".$config["base_url"]."/".$mode."', '".$config["admin_passwd"]."');\">".$mode."</a>";
}

/*
 * Cleanup the whole array
 */
function cleanup_array($arr) {
    if (is_array($arr) && is_array($arr['gateway'])) {
        $arr = $arr['gateway'];
        clean_branch($arr, 'wdp');
        clean_branch($arr, 'sms');
        clean_branch($arr, 'dlr');
        $arr['boxes'] = $arr['boxes'][0]['box'];
        $arr['smscs'] = $arr['smscs'][0]['smsc'];
    }
    return $arr;
}

/*
 * Cleanup the branches to fold unnecessary levels
 */
function clean_branch(&$arr, $tag='') {
    $fields = array('received', 'sent');
    if ($tag) {
        $arr[$tag] = array_shift($arr[$tag]);
    }
    foreach ($fields as $key) {
        if ($tag) {
            if (is_array($arr[$tag][$key])) {
                $arr[$tag][$key] = array_shift($arr[$tag][$key]);
            }
        } else {
            if (is_array($arr[$key])) {
                $arr[$key] = array_shift($arr[$key]);
            }
        }
    }
}

/*
 * Get a path/of/xml/nodes from an array
 */
function get_path($arr, $path) {
    $parts = explode("/", $path);
    if (!is_array($arr) || !is_array($parts)) {
        return $arr;
    }
    foreach($parts as $part) {
        $arr = $arr[$part];
    }
    return $arr;
}
?>