#!/usr/bin/perl
require HTML::TokeParser;

# html2wml 0.1 13/01/2000
# Taneli Leppa <rosmo@SEKTORI.COM>
# License: public domain


$error_cardstart = "<card id=\"error\" title=\"Error\" newcontext=\"true\">\n";
$error_cardend   = "</card>\n";
$error_filenotfound = $error_cardstart . "<p>The file was not found.</p>\n" . $error_cardend;

$HTML_HEAD = 0x0001;
$HTML_UL   = 0x0002;
$HTML_NL   = 0x0004;

# Print default headers
# 

$wml = q{<?xml version="1.0"?>
<!DOCTYPE wml PUBLIC "-//WAPFORUM//DTD WML 1.1//EN" "http://www.wapforum.org/DTD/wml_1.1.xml">

<wml>
};

# Read in the HTML
#

if (!($p = HTML::TokeParser->new($ARGV[0]))) 
  { $wml .= $error_filenotfound; goto "FINISH"; }

$p->get_tag("title");
$title = $p->get_text;
$wml .= "<card id=\"foo\" title=\"$title\">\n";

if (!($p = HTML::TokeParser->new($ARGV[0]))) 
  { $wml .= $error_filenotfound; goto "FINISH"; }

$hide_text = 0;
$current_place = 0;

while ($token = $p->get_token)
{
    $_ = $token->[0];
    TAGTYPE: {
	/S/ &&  do { $wmlbit = start_tag($token->[1], $token->[2]); last TAGTYPE; };
	/E/ &&  do { $wmlbit = end_tag($token->[1]); last TAGTYPE; };
	/T/ &&  do { $wmlbit = $token->[1]; chomp $wmlbit; last TAGTYPE; };
#	/D/ &&  do { $text = $token->[0]; last TAGTYPE; };
#	/PI/ && do { $text = $token->[0]; last TAGTYPE; };
    }
    if (!$hide_text) { $wml .= $wmlbit; }
}

close(F);

FINISH:

$wml .= "\n</card>\n</wml>";

print length($wml), "\n";
print $wml;

sub start_tag {
    local($tag, $attrs) = @_;
    my $s;
   
    if (uc($tag) eq "LI")
    {
      if ($current_place & $HTML_UL)
       { if ($list_index > 1) { $s = "<BR/>"; } $s .= "* "; }  

      if ($current_place & $HTML_OL)
       { if ($list_index > 1) { $s = "<BR/>"; } $s .= " " . $list_index . ". "; }  

      $list_index++;
    }

        

    if (uc($tag) eq "BR")
    {  
	$s = "<BR/>";
    }

    if (uc($tag) eq "UL")
    {
	$list_index = 1;
	$current_place |= $HTML_UL;
	$s = "<BR/>";
    }

    if (uc($tag) eq "NL")
    {
	$list_index = 1;
	$current_place |= $HTML_NL;
	$s = "<BR/>";
    }

    if (uc($tag) eq "H1" || uc($tag) eq "H2")
    { 
	$s = "<STRONG>";
    } 

    if (uc($tag) eq "H3" || uc($tag) eq "H4")
    { 
	$s = "<EM>";
    } 

    if (uc($tag) eq "H5" || uc($tag) eq "H6")
    { 
	$s = "<B>";
    } 

    if (uc($tag) eq "HEAD")
    {
	$current_place |= $HTML_HEAD;
	$hide_text = 1;
    }

    if (uc($tag) eq "IMG")
    {
	if ($attrs->{"alt"} ne "")
        {
	    $s = $attrs->{"alt"};
	}
    }

    if (uc($tag) eq "TEXTAREA")
    {
	$s = "<INPUT type=\"text\" name=\"" . $attrs->{"name"} . "\"";
	if ($attrs->{"size"} ne "") { $s .= " size=\"" . $attrs->{"size"} . "\""; } 
	$s .= " value=\"";
	push @form_dynamic_vars, $attrs->{"name"};
    }

    if (uc($tag) eq "INPUT")
    {

	if (uc($attrs->{"type"}) eq "TEXT")
	{
	    $s = "<INPUT type=\"text\" name=\"" . $attrs->{"name"} . "\" value=\"" . $attrs->{"value"} . "\"";
	    if ($attrs->{"size"} ne "") { $s .= " size=\"" . $attrs->{"size"} . "\""; } 
            $s .= "/>\n";
	    push @form_dynamic_vars, $attrs->{"name"};
	}

	if (uc($attrs->{"type"}) eq "SUBMIT") 
	{
	    $form_submit_label = $attrs->{"value"};
	}

	if (uc($attrs->{"type"}) eq "PASSWORD") 
	{
	    $s = "<INPUT type=\"text\" name=\"" . $attrs->{"name"} . "\"";
	    if ($attrs->{"size"} ne "") { $s .= " size=\"" . $attrs->{"size"} . "\""; } 
	    $s .= "/>";
	    push @form_dynamic_vars, $attrs->{"name"};             
	}

	if (uc($attrs->{"type"}) eq "HIDDEN") 
	{
	    push @form_static_vars, ($attrs->{"name"}, $attrs->{"value"});
	}
    }

    if (uc($tag) eq "FORM")
    {
	reset(@form_dynamic_vars);
	reset(@form_static_vars);
        reset $form_submit_label;
	$form_method = $attrs->{"method"};
	$form_action = $attrs->{"action"};
    }


    if (uc($tag) eq "A")
    {
     $s = "<A href=\"" . $attrs->{"href"} . "\">";
    }

    if (uc($tag) eq "B")
    {
	$s = "<EM>";
    }

    if (uc($tag) eq "I" || uc($tag) eq "U")
    {
	$s = "<" . uc($tag) . ">";
    }

    if (uc($tag) eq "P")
    {
	$s = "<P/>";
    }

    return $s; 
}

sub end_tag {
    local($tag) = @_;
    my $s;


    if (uc($tag) eq "UL")
    {
	$current_place ^= $HTML_UL;
	$s = "<BR/>";
    }

    if (uc($tag) eq "NL")
    {
	$current_place ^= $HTML_NL;
	$s = "<BR/>";
    }
   
    if (uc($tag) eq "H1" || uc($tag) eq "H2")
    { 
	$s = "</STRONG><BR/>";
    } 

    if (uc($tag) eq "H3" || uc($tag) eq "H4")
    { 
	$s = "</EM><BR/>";
    } 

    if (uc($tag) eq "H5" || uc($tag) eq "H6")
    { 
	$s = "</B><BR/>";
    } 

    if (uc($tag) eq "HEAD")
    {
	$current_place ^= $HTML_HEAD;
	$hide_text = 0;
    }

    if (uc($tag) eq "STYLE")
    {
	if ($current_place & $HTML_HEAD) { $hide_text = 0; }
    }

    if (uc($tag) eq "TEXTAREA")
    {
	$s = "\"/>";
    }

    if (uc($tag) eq "FORM")
    {
	$s =  "\n<DO type=\"accept\" label=\"$form_submit_label\">\n";
        if (uc($form_method) eq "") { $form_method = "GET"; }
        if (uc($form_method) eq "POST") 
	{
	    $s .= "\t<GO method=\"POST\" href=\"$form_action\">\n";
	    $z = pop @form_static_vars;
	    while (defined($z)) 
	    {
	        $x = pop @form_static_vars;
		$s .= "\t\t" . "<POSTFIELD name=\"$z\" value=\"$x\">\n";
	        $z = pop @form_static_vars;
            }
	    foreach $z (@form_dynamic_vars)
	    {
		$s .= "\t\t" . "<POSTFIELD name=\"$z\" value=\"\${$z}\">\n";
            }
	    $s .= "\n\t</GO>\n";
        }

        if (uc($form_method) eq "GET") 
	{
	    $s .= "\t<GO href=\"$form_action?"; 
      
   	    $z = pop @form_static_vars; $i = 0;
            while (defined($z)) 
	    {
		if ($i++ > 0) { $s .= "&"; } 
		$s .= $z;
                $z = pop @form_static_vars;                
		if (defined($z)) 
                  { 
		   $s .= "=" . $z; 
                   $z = pop @form_static_vars;                
		  }
            }
   	    $z = pop @form_dynamic_vars;
            while (defined($z)) 
	    {
		if ($i++ > 0) { $s .= "&amp;"; } 
		$s .= $z . "=\${" . $z . "}";
                $z = pop @form_dynamic_vars;                
            }
	    $s .= "\"/>\t</GO>\n";
        }
	$s .= "</DO>\n";
    }

    if (uc($tag) eq "A")
    {
	$s = "</A>";
    }

    if (uc($tag) eq "B")
    {
	$s = "</EM>";
    }

    if (uc($tag) eq "I" || uc($tag) eq "U")
    {
	$s = "</" . uc($tag) . ">";
    }

    return $s;
}

exit 0;
