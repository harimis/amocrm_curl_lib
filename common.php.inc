<?php
require_once "config.php.inc";
require_once "logger.php.inc";

$cookieName = null; // имя файла cookie для данного процесса. 
/**
 * Проверяет код HTTP на ошибку. Прерывает выполнение в случае ошибки.
 * @param $code - код ошибки для проверки.
 * @throws Exception If код индицирует ошибоку
 */
function CheckCurlResponse($code) {
	$code=(int)$code;
	$errors=array(
		301=>'Moved permanently',
		400=>'Bad request',
		401=>'Unauthorized',
		403=>'Forbidden',
		404=>'Not found',
		500=>'Internal server error',
		502=>'Bad gateway',
		503=>'Service unavailable'
	);
	if($code!=200 && $code!=204)
		throw new Exception(isset($errors[$code]) ? $errors[$code] : 'Undescribed error '.$code,$code);
}

/**
 * Пытается авторизироваться в amoCRM.
 * @param  array $user_data	- аргументы для передачи POST вместо логина и пароля по умолчанию.
 * @param  string $subdomain - субдомен (аккаунт amoCRM)
 * @return boolean - успех операции. true если авторизация успешна.
 */
function amo_curl_authorize($user_data=null, $subdomain=AMOCRM_SUBDOMAIN) {
	global $cookieName;

	if (empty($cookieName)) {
		$cookieName = tempnam (__DIR__, "ckie_");
		register_shutdown_function(cookie_crusher);
	}

	if (is_null($user_data))
		$user_data = array('USER_LOGIN' => AMOCRM_MASTER_LOGIN,
						   'USER_HASH' => AMOCRM_MASTER_HASH);
	$link='https://'.$subdomain.'.amocrm.ru/private/api/auth.php?type=json';
	$curl=curl_init(); #Сохраняем дескриптор сеанса cURL
	#Устанавливаем необходимые опции для сеанса cURL
	curl_setopt($curl,CURLOPT_RETURNTRANSFER,true);
	curl_setopt($curl,CURLOPT_USERAGENT,'amoCRM-API-client/1.0');
	curl_setopt($curl,CURLOPT_URL,$link);
	curl_setopt($curl,CURLOPT_POST,true);
	curl_setopt($curl,CURLOPT_POSTFIELDS,http_build_query($user_data));
	curl_setopt($curl,CURLOPT_HEADER,false);
	curl_setopt($curl,CURLOPT_COOKIEFILE, $cookieName); #PHP>5.3.6 dirname(__FILE__) -> __DIR__
	curl_setopt($curl,CURLOPT_COOKIEJAR, $cookieName); #PHP>5.3.6 dirname(__FILE__) -> __DIR__
	curl_setopt($curl,CURLOPT_SSL_VERIFYPEER,0);
	curl_setopt($curl,CURLOPT_SSL_VERIFYHOST,0);
 
	$out=curl_exec($curl); #Инициируем запрос к API и сохраняем ответ в переменную
	$code=curl_getinfo($curl,CURLINFO_HTTP_CODE);
	curl_close($curl);

	try {
		CheckCurlResponse($code);
	}
	catch(Exception $E)	{
		$err_mess = 'Ошибка: '.$E->getMessage().PHP_EOL.'Код ошибки: '.$E->getCode();
		log_event($err_mess, "ERROR");
		die($err_mess);
	}

	$response=json_decode($out,true);

	#Флаг авторизации доступен в свойстве "auth"
	return isset($response['response']['auth']);
}

/**
 * Выполняет запрос POST по REST API через CURL и возвращает массив полученных данных. 
 * @param  string $url - (!) относительный адрес для запроса
 * @param  array $postData - данные для отправки POST
 * @param  string $subdomain - субдомен (аккаунт amoCRM)
 * @return массив присланный в ответ на запрос или null, если нет данных. В случае ошибки выполнение прерывается.
 */
function amo_curl_post($url, $postData, $subdomain=AMOCRM_SUBDOMAIN) {
	global $cookieName;

	if (empty($cookieName)) {
		$cookieName = tempnam (__DIR__, "ckie_");
		register_shutdown_function(cookie_crusher);
	}

	$link='https://'.$subdomain.'.amocrm.ru'.$url;
	$curl=curl_init(); #Сохраняем дескриптор сеанса cURL
	#Устанавливаем необходимые опции для сеанса cURL
	curl_setopt($curl,CURLOPT_RETURNTRANSFER,true);
	curl_setopt($curl,CURLOPT_USERAGENT,'amoCRM-API-client/1.0');
	curl_setopt($curl,CURLOPT_URL,$link);
	curl_setopt($curl,CURLOPT_CUSTOMREQUEST,'POST');
	curl_setopt($curl,CURLOPT_POSTFIELDS,json_encode($postData));
	curl_setopt($curl,CURLOPT_HTTPHEADER,array('Content-Type: application/json'));
	curl_setopt($curl,CURLOPT_HEADER,false);
	curl_setopt($curl,CURLOPT_COOKIEFILE, $cookieName); #PHP>5.3.6 dirname(__FILE__) -> __DIR__
	curl_setopt($curl,CURLOPT_COOKIEJAR, $cookieName); #PHP>5.3.6 dirname(__FILE__) -> __DIR__
	curl_setopt($curl,CURLOPT_SSL_VERIFYPEER,0);
	curl_setopt($curl,CURLOPT_SSL_VERIFYHOST,0);
 
	$out=curl_exec($curl); #Инициируем запрос к API и сохраняем ответ в переменную
	$code=curl_getinfo($curl,CURLINFO_HTTP_CODE);
	curl_close($curl);
	try {
		CheckCurlResponse($code);
	}
	catch(Exception $E)	{
		$err_mess = 'Ошибка: '.$E->getMessage().PHP_EOL.'Код ошибки: '.$E->getCode();
		log_event($err_mess, "ERROR");
		die($err_mess);
	}

	$Response=json_decode($out,true);
	return $Response['response'];
}

/**
 * Выполняет запрос GET по REST API через CURL и возвращает массив полученных данных.
 * @param  string $url - (!) относительный адрес для запроса.
 * @param  string $dataType - имя запрашиваемых данных ('leads', 'contacts'...) для использования многостраничного 
 * запроса (limit_rows и limit_offset).
 * Если null, то многостраничный запрос не используется.
 * @param  array $args - данные для отправки GET. Если указан аргумент 'limit_rows', то он обрабатывается без ограничения в 500 элементов, 
 * описаного в API. Аргумент $dataType не должен быть null для этого.
 * @param  long $modifiedSince - метка времени для передачи в виде параметра modified-since.
 * @param  string $subdomain - субдомен (аккаунт amoCRM).
 * @return часть ['response'] массива присланного в ответ на запрос или null, если нет данных. В случае ошибки выполнение прерывается.
 */
function amo_curl_get($url, $dataType=null, $args=null, $modifiedSince=null, $subdomain=AMOCRM_SUBDOMAIN) {
	global $cookieName;

	if (empty($cookieName)) {
		$cookieName = tempnam (__DIR__, "ckie_");
		register_shutdown_function(cookie_crusher);
	}

	$curl=curl_init(); #Сохраняем дескриптор сеанса cURL
	#Устанавливаем необходимые опции для сеанса cURL
	curl_setopt($curl,CURLOPT_RETURNTRANSFER,true);
	curl_setopt($curl,CURLOPT_USERAGENT,'amoCRM-API-client/1.0');
	curl_setopt($curl,CURLOPT_HEADER,false);
	if (!is_null($modifiedSince)) {
		$dateTime = new DateTime(); // время хоста
		$dateTime->setTimestamp($modifiedSince); // время по метке
        $dateTime->setTimezone(new DateTimeZone(AMOCRM_ACCOUNT_TIMEZONE));
		$sDate = $dateTime->format("D, d M Y H:i:s");
		curl_setopt($curl, CURLOPT_HTTPHEADER, array("if-modified-since: $sDate"));
	}
	curl_setopt($curl,CURLOPT_COOKIEFILE, $cookieName); #PHP>5.3.6 dirname(__FILE__) -> __DIR__
	curl_setopt($curl,CURLOPT_COOKIEJAR, $cookieName); #PHP>5.3.6 dirname(__FILE__) -> __DIR__
	curl_setopt($curl,CURLOPT_SSL_VERIFYPEER,0);
	curl_setopt($curl,CURLOPT_SSL_VERIFYHOST,0);
 

	$retry_counter = 3; $completion_flag = false;
	while ($retry_counter && !$completion_flag) {

				if (is_null($args)) $args = array();
				$offset = 0; $step = 400; // по умолчанию
				if (isset($args['limit_offset']) && $args['limit_offset']>=0)
					$offset = (int) $args['limit_offset'];
				$limit_rows = $args['limit_rows'];

				$results = array( $dataType => array());
				$completion_flag = true; // надежда...
				do {
					if (!is_null($dataType)) {
						// echo "*limit_rows == $limit_rows, *step == $step";
						$args['limit_rows'] = ($limit_rows == 0 || $limit_rows == null)? $step : min($step, $limit_rows);
						$args['limit_offset'] = $offset;
					}
					$link='https://'.$subdomain.'.amocrm.ru'.$url.'?'.http_build_query($args);
					log_event("query is ".$link);
					curl_setopt($curl,CURLOPT_URL,$link);

					$out=curl_exec($curl); #Инициируем запрос к API и сохраняем ответ в переменную
					$code=curl_getinfo($curl,CURLINFO_HTTP_CODE);

					try {
						CheckCurlResponse($code);
					}
					catch(Exception $E)	{
						$err_mess = 'Ошибка: '.$E->getMessage().PHP_EOL.'Код ошибки: '.$E->getCode();
						log_event($err_mess.".... retrying.", "ERROR");
						$completion_flag = false; // .. надежда не оправдалась
						$retry_counter--;
						sleep(1);
						break; // while (count($chunk[$dataType])==$step)
					}

					$response=json_decode($out,true);

					$chunk = $response['response'];

					// если тип данных не указан, то возвращаем первый результат.
					if (is_null($dataType))
						return $chunk;

					// log_event("Получены элементы $dataType ".$offset." .. ".($offset+count($chunk[$dataType])),"DEBUG");
					if (!is_null($chunk)) {
						if (isset($limit_rows))
							$limit_rows -= count($chunk[$dataType]);
						$results[$dataType] = array_merge($results[$dataType], $chunk[$dataType]);
						$results['server_time'] = $chunk['server_time'];
					}

					// log_event("got ".count($chunk[$dataType])." from ".$offset);
					$offset += $step;
				} while (count($chunk[$dataType])==$step && $limit_rows>0);
				// log_event("Download cycle complete, completion flag is ".($completion_flag? "set": "dropped"),"DEBUG");
	}

	curl_close($curl);
	if (!$completion_flag) {
		die(); // закончить выполнение, т.к. в cURL произошла неисправимая ошибка
	}
	return $results;
}

function amo_curl_get_mass($url, $dataType, $args=null, $prevdata=null, $modifiedSince=null, $processor=null, $subdomain=AMOCRM_SUBDOMAIN) {
	global $params;

	$args = is_null($args)? array() : $args;
	$total_items = isset($args['limit_rows'])? $args['limit_rows']: -1;
	$original_offset = isset($args['limit_offset'])? $args['limit_offset']: 0;

	$result_data = empty($prevdata)? array(): $prevdata;

	$args['limit_rows'] = ($total_items>0)? min($total_items, 400) : 400;
	$args['limit_offset'] = $original_offset;

	do {
		$batch = amo_curl_get($url, $dataType, $args, $modifiedSince, $subdomain);

		foreach ($batch[$dataType] as $element) {
			$processed_element = is_null($processor)? $element : $processor($element);
			if (isset($element['id']))
				$result_data[$element['id']] = $processed_element;
			else
				$result_data[] = $processed_element;
		}

		$args['limit_offset'] += 400;
	} while (count($batch['contacts']) == 400);

	return $result_data;
}

function cookie_crusher() {
	global $cookieName;

	if (!empty($cookieName))
		unlink($cookieName);
}
?>