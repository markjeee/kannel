<?php
    /*
     * Configure the kannel instances here
     */
    $configs = array(
        array( "base_url" => "http://kannel.yourdomain.com:23000",
               "status_passwd" => "foobar",
               "admin_passwd" => "",
               "name" => "Kannel 1"
             ),
        array( "base_url" => "http://kannel.yourdomain.com:33000",
               "status_passwd" => "foobar",
               "admin_passwd" => "",
               "name" => "Kannel 2"
             )
    );

    /* some constants */
    define('MAX_QUEUE', 100); /* Maximum size of queues before displaying it in red */
    define('DEFAULT_REFRESH', 60); /* Default refresh time for the web interface */
?>
