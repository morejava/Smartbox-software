<?php  

   $data_file = "/tmp/curr_data.json";
   $conf_file = "/media/data/thermostat.json";
   
   if($_SERVER['REQUEST_METHOD'] == "POST") 
   {   
      // check for required parameter
      if (!empty($_POST['tempt']) && !empty($_POST['operation_mode']))
      {
         $opmode = $_POST['operation_mode'];
         $tempt = $_POST['tempt'];
         $timestamp = time();
         
         // replace target temperature in data file
         $data = json_decode(file_get_contents($data_file));   
         $data->{'tempt'} = $tempt;
         file_put_contents($data_file, json_encode($data));
         
         // replace operation mode and target temperature in thermostat conf file
         $conf = json_decode(file_get_contents($conf_file));
         $conf->{'operation_mode'} = $opmode;
         switch ($opmode)
         {
            case "temp_override":
               $conf->{'temp_override'}->{'temp'} = $tempt;
               $conf->{'temp_override'}->{'time_stamp'} = $timestamp;
               break;
            case "immediate":
               $conf->{'immediate'}->{'temp'} = $tempt;
               break;
         }
         file_put_contents($conf_file, json_encode($conf));
      }
   }
   else
   {
      // return content of the data file
      echo file_get_contents($data_file);
   }
?>
