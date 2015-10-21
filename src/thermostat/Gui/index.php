<!DOCTYPE html PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN" "http://www.w3.org/TR/html4/loose.dtd">
<html dir="ltr" lang="en">
  <head>
    <meta content="text/html; charset=utf-8" http-equiv="content-type">
    <meta content="Ondrej Wisniewski" name="author">
    <meta name="viewport" content="width=device-width, height=device-height, initial-scale=1, user-scalable=no">
    <link rel="stylesheet" type="text/css" href="style.css" />
    <title>Thermostat GUI</title>
    <script type="text/javascript" src="jquery-1.11.1.min.js"></script> 
    
    <?php 
    
      // read parameters from global config file
      $conf_file = "/etc/telegea.conf";
      $config = parse_ini_file($conf_file, true);
      $plantid = $config[generic][PLANTID];
      $apiurl = $config[generic][TELEGEA_API];
      $apikey = $config[generic][API_KEY];
      $control1 = $config[thermostat][HEATCOOL_CTRL1_REG_NAME];
      $control2 = $config[thermostat][HEATCOOL_CTRL2_REG_NAME];
      $user_mode = $config[thermostat][USER_MODE];
      $location = $config[thermostatgui][LOCATION];
      $owmapi = $config[thermostatgui][OWM_API];
      $owmappid = $config[thermostatgui][OWM_APPID];

      // sanity checks
      $webapi_enabled = ($apikey=="" || $plantid=="") ? "false" : "true";
      $weather_enabled = ($location=="") ? "false" : "true";
      
      // gui configuration settings
      $cntr1_visibility = ($control1=="") ? "hidden" : "visible";
      $cntr2_visibility = ($control2=="") ? "hidden" : "visible";
      $operation_mode = ($user_mode=="smart") ? "temp_override" : "immediate";
      
      // print debug info as html comments
      echo "\n";
      echo "<!-- plantid=$plantid -->\n";
      echo "<!-- location=$location -->\n";
      echo "<!-- apiurl=$apiurl -->\n";
      echo "<!-- apikey=$apikey -->\n";
      echo "<!-- control1=$control1 -->\n";
      echo "<!-- control2=$control2 -->\n";
      echo "<!-- webapi_enabled=$webapi_enabled -->\n";
      echo "<!-- weather_enabled=$weather_enabled -->\n";
      echo "<!-- operation_mode=$operation_mode -->\n";
      
    ?>
    
    <script type="text/javascript"> 
        
      var int_timer;
      var tmo_timer;
      
      $(document).ready(function() {
      
        getUpdates(); 
        getTime();
        
        if (<?php echo $weather_enabled?>) getWeather();
                  
        // check for new updates periodically
        int_timer = setInterval('getUpdates()',5000); // 5s 
        
        // update clock periodically
        setInterval('getTime()',30000); // 30s 
        
        // update weather info periodically
        setInterval('getWeather()',600000); // 10min
      });
                
      function getUpdates() {

        // get the latest data from the local json file
        $.getJSON("input-handler.php", function(data) {
                                               
          var str;
                           
          // Current temperature
          str= data.tempc;
          str= str.split(".",2);
          document.getElementById("tempc_i").innerHTML= str[0];
          document.getElementById("tempc_f").innerHTML= str[1] + "°";
                           
          // Target temperature
          str= data.tempt;
          str= str.split(".",2);
          str[1] = "0";
          document.getElementById("tempt_i").innerHTML= str[0];
          document.getElementById("tempt_f").innerHTML= str[1] + "°";
                           
          // Control state 1 (radiator)
          document.getElementById("radiator_icon").style.visibility = "<?php echo $cntr1_visibility?>";
          if (data.state1 == 1) {
            document.getElementById("radiator_icon").innerHTML= '<img style="width: 30px; height: 30px;" src="icon-radiator.png" ondragstart="return false;">';
          }
          else {
            document.getElementById("radiator_icon").innerHTML= '<img style="width: 30px; height: 30px;" src="icon-radiator-off.png" ondragstart="return false;">';
          }
          
          // Control state 2 (fan coil)
          document.getElementById("fan_icon").style.visibility = "<?php echo $cntr2_visibility?>";
          if (data.state2 == 1) {
            document.getElementById("fan_icon").innerHTML= '<img style="width: 30px; height: 30px;" src="icon-fan.png" ondragstart="return false;">';
          }
          else {
            document.getElementById("fan_icon").innerHTML= '<img style="width: 30px; height: 30px;" src="icon-fan-off.png" ondragstart="return false;">';
          }
          
          // Season mode
          if (data.mode == "cooling") {
            document.getElementById("season_icon").innerHTML= '<img style="width: 30px; height: 30px;" src="icon-summer.png" ondragstart="return false;">';
          }
          else {
            document.getElementById("season_icon").innerHTML= '<img style="width: 30px; height: 30px;" src="icon-winter.png" ondragstart="return false;">';
          }
        });
      }

      function getWeather() {
                  
        // get the latest weather data for our location from OpenWeatherMap with a webservice call
        $.getJSON('<?php echo $owmapi?>?q=<?php echo $location?>&appid=<?php echo $owmappid?>&callback=?', function(data) {
                                                                                
          //document.getElementById("weather_cond").innerHTML= data.weather[0].description;
          document.getElementById("weather_icon").innerHTML= '<img style="vertical-align: middle; width: 50px; height: 50px;" src="http://openweathermap.org/img/w/'+data.weather[0].icon+'.png" ondragstart="return false;"/>';
          var temp_ext= parseInt(data.main.temp - 273.15);
          document.getElementById("temp_ext").innerHTML= temp_ext + "°";
        });
      }
      
      function getTime() {
        var d = new Date();
        var hh=d.getHours();
        var mm=d.getMinutes();
                   
        var hhStr = (hh < 10) ? '0' + hh : hh;
        var mmStr = (mm < 10) ? '0' + mm : mm;
        document.getElementById("curr_time").innerHTML = hhStr + ":" + mmStr;
      }
                
      function increaseTempt() {
        modifyTempt(1);
      }
                
      function decreaseTempt() {
        modifyTempt(-1);
      }
                
      function modifyTempt(delta) {

        // stop ongoing timers
        clearInterval(int_timer);
        clearTimeout(tmo_timer);
        
        // change colour of number being modified
        document.getElementById("tempt_i").style.color = "rgb(255, 204, 255)";
        document.getElementById("tempt_f").style.color = "rgb(255, 204, 255)";
        
        // increase target temperature in small steps (for now only integer)
        var temp_t = parseInt(document.getElementById("tempt_i").innerHTML) + 
                     parseInt(document.getElementById("tempt_f").innerHTML)/10 + 
                     delta;
                     
        if (temp_t > 35) temp_t=35.0;
        if (temp_t < 5) temp_t=5.0;
                                
        document.getElementById("tempt_i").innerHTML = parseInt(temp_t);

        // send data after timeout
        var plant_id = "<?php echo $plantid?>";
        var apikey = "<?php echo $apikey?>";
        var operation_mode = "<?php echo $operation_mode?>";
        //var operation_mode = "immediate";
        var data = {plant_id: plant_id, apikey: apikey, tempt: temp_t, operation_mode: operation_mode};
        tmo_timer = setTimeout(function() {sendData(data);},3000);
      }
      
      function sendData(data) {
      
        // jQuery post request to input handler php script
        $.post("input-handler.php", data);
        
        // jQuery post request to TeleGea web_api
        if (<?php echo $webapi_enabled?>)
           $.post("<?php echo $apiurl?>/thermostat.php", data);
        
        // restore default colour of number being modified
        document.getElementById("tempt_i").style.color = "white";
        document.getElementById("tempt_f").style.color = "white";
        
        // restart update interval timer 
        int_timer = setInterval('getUpdates()',5000); // 5s 
      }
                
    </script>
  </head>
  
  <body class="touchscreen" style="background-image: url(background_SM.jpg); background-size: 103% 103%"> 
  
   <div id="outer">
      <div id="container">
        <table id="inner">
          <tbody>
            <tr>
              <td class="td_current_temp">
                <img class="icon-thermostat" alt="" src="icon-thermometer.png" ondragstart="return false;">
                <span id="tempc_i">00</span>.<span id="tempc_f">0°</span><br>
                <span id="radiator_icon"></span>
                <span id="fan_icon"></span>
                <span id="season_icon"></span>
              </td>

              <td class="td_target_temp">
                <img onmousedown="increaseTempt()" class="icon-arrow-up" alt="" src="icon-arrow-up.png" ondragstart="return false;"><br>
                <span id="tempt_i">00</span>.<span id="tempt_f">0°</span><br>
                <img onmousedown="decreaseTempt()" class="icon-arrow-down" alt="" src="icon-arrow-down.png" ondragstart="return false;"><br>
              </td>
            </tr> 
            <tr>
              <td class="td_weather_clock">
                <span id="weather_icon"></span>
                <span id="temp_ext"></span>
              </td>
              <td class="td_weather_clock">
                <!-- <img style="width: 32px; height: 32px;" alt="" src="icon-clock.png" hspace="4" ondragstart="return false;"> -->
                <span id="curr_time">00:00</span>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
   </div>

</body></html>
