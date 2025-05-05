#!/bin/bash
curl 'http://127.0.0.1:59923/status'
curl 'http://127.0.0.1:59923/users/login?user=user&password=password'
curl 'http://127.0.0.1:59923/users/login?user=user&password=password'

for (( a = 1; a < 1000000; a++ )) 
do
curl 'http://127.0.0.1:59923/status' &
curl 'http://127.0.0.1:59923/data/stockexchanges?sessionId=1' &
curl 'http://127.0.0.1:59923/users/config?sessionId=1&config=eyJGaWx0ZXIiOnsiRmlsdGVycyI6W3siRGVsdGEiOjUwMCwiVm9sdW1lIjo1MDB9XX19' &
curl 'http://127.0.0.1:59923/data/detect?sessionId=1' &
curl 'http://127.0.0.1:59923/data/stockexchanges?sessionId=2' &
curl 'http://127.0.0.1:59923/users/config?sessionId=2&config=eyJGaWx0ZXIiOnsiRmlsdGVycyI6W3siRGVsdGEiOjUwMCwiVm9sdW1lIjo1MDB9XX19' &
curl 'http://127.0.0.1:59923/data/detect?sessionId=2'

done

curl 'http://127.0.0.1:59923/users/logout?sessionId=1'
curl 'http://127.0.0.1:59923/users/logout?sessionId=2'

