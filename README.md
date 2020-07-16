wb-mqtt-serial
==============

Serial device <==> MQTT bridge which follows [Wiren Board MQTT Conventions](https://github.com/contactless/homeui/blob/contactless/conventions.md).
It's designed to be used on [Wiren Board](http://contactless.ru/en/) family of programmable automation controllers (PACs), although there is nothing that prevents it from running on a arbitrary Linux machine.

Драйвер master-slave протоколов для устройств, работающих через
последовательный порт. В драйвере wb-mqtt-serial реализована
поддержка некоторых устройств, работающих по протоколам Modbus,
[Uniel](http://smart.uniel.ru),
[ИВТМ](http://www.eksis.ru/catalog/measures-of-relative-humidity-and-temperature/),
[Меркурий 230](http://www.incotexcom.ru/m230art.htm),
[Милур](http://www.milur.ru/).

Запускается командой `/etc/init.d/wb-mqtt-serial start`
По умолчанию запуск драйвера происходит при загрузке системы при
наличии конфигурационного файла `/etc/wb-mqtt-serial.conf`. При
установке пакета  конфигурационный файл, в котором не описано ни одного подключенного устройства.
При старте по умолчанию драйвер завершается с ошибкой. Добавьте устройства в `/etc/wb-mqtt-serial.conf` либо воспользуйтесь онлайн-редактором настроек для начала работы.

Возможен запуск демона вручную, что может быть полезно
для работы в отладочном режиме:

```
# service wb-mqtt-serial stop
# wb-mqtt-serial -c /etc/wb-mqtt-serial.conf -d
```

Конфигурационный файл построен по трёхуровневой схеме:
порты (ports) -> устройства (devices) -> каналы (channels).
Конфигурация устройства device может быть задана двумя способами:
вручную прописать все параметры или задать только пять параметров:
```
{
    // По DeviceType драйвер будет искать параметры в папке
    // /usr/share/wb-mqtt-serial/templates.
    "device_type" : "DeviceType",

    // отображаемое имя устройства. Публикуется как
    // .../meta/name в MQTT
    // По умолчанию name берется из шаблона и добавляется slave_id, т.е.
    // "name" + " " + "slave_id"
    "name" : "somename",

    // уникальный идентификатор устройства в MQTT.
    // каждое элемент в devices должен иметь уникальный id
    // topic'и, относящиеся в MQTT к данному устройству,
    // имеют общий префикс /devices/<идентификатор топика>/...
    // также по умолчанию берется из шаблона с добавлением slave_id:
    // "deviceID" + "_" + slave_id
    "id" : "deviceID",

    // идентификатор slave
    "slave_id" : slaveID,

    // включить/выключить устройство. В случае задания
    // "enabled": false опрос устройства и запись значений
    // его каналов не происходит. По умолчанию - true.
    "enabled" : true,

    // если используется шаблон устройства, определения
    // каналов совмещаются. Если имя (name) в определении
    // канала устройства совпадает с именем канала в шаблоне,
    // свойства каналов из шаблона и определения устройства
    // совмещаются, при этом значения свойств из определения
    // устройства (в файле конфигурации) имеют преимущество.
    // Это можно использовать, например, для задания индивидуальных
    // интервалов опроса каналов. Если канал с таким же
    // именем, как канал в определении устройства, отсутствует
    // в шаблоне, создаётся новый канал.
    "channels": [
        {
            // имя канала. topic'и, соответствующие каналу,
            "name" : "Temp 1",
            "poll_interval": 10000
        }
    ]
}
```
Структура папки *templates* такова, что в каждом файле
приведены параметры для одного типа устройств.
Также можно совместить первый способ со вторым, к вышеприведенным
5 параметрам дописать конфигурацию для каналов, которые не прописаны
в соответствующем файле в папке templates.
См. также: [пример конфигурационного файла с использованием шаблонов](config.json).

Ниже приведён пример конфигурационного файла /etc/wb-mqtt-serial.conf

```
{
    // опция debug включает или выключает отладочную печать.
    // Опция -d командной строки wb-mqtt-serial также
    // включает отладочную печать и имеет приоритет над
    // данной опцией.
    "debug": false,

    // список портов
    "ports": [
        {
            // тип порта - serial: RS-485, tcp: TCP/IP (по умолчанию - serial)
            "port_type": "serial",

            // устройство, соответствующее порту RS-485 (если выбран тип порта serial)
            "path" : "/dev/ttyNSC0",

            // IP адрес или имя хоста (если выбран тип порта TCP)
            "address": "127.0.0.1",

            // TCP порт (если выбран тип порта TCP)
            "port": 3000,

            // скорость порта
            "baud_rate": 9600,

            // паритет - N, O или E (по умолчанию - N)
            "parity": "N",

            // количество бит данных (по умолчанию - 8)
            "data_bits": 8,

            // количество стоп-бит
            "stop_bits": 2,

            // Минимальный интервал опроса каждого регистра
            // по-умолчанию для устройств, подключенных к порту,
            // в миллисекундах

            "poll_interval": 10,

            // Дополнительная задержка перед каждой отправкой данных в порт,
            // в микросекундах
            "guard_interval_us": 1000,

            // Таймаут соединения (только для TCP порта).
            // Если в течении указанного времени от порта не поступило данных,
            // TCP соединение будет разорвано и произойдет попытка переподключения
            "connection_timeout_ms": 5000,

            // включить/выключить порт. В случае задания
            // "enabled": false опрос порта и запись значений
            // каналов в устройства на данном порту не происходит.
            // По умолчанию - true.
            "enabled": true,

            // список устройств на данном порту
            "devices" : [
                {
                    // отображаемое имя устройства. Публикуется как
                    // .../meta/name в MQTT
                    "name": "MSU34+TLP",

                    // уникальный идентификатор устройства в MQTT.
                    // каждое элемент в devices должен иметь уникальный id
                    // topic'и, относящиеся в MQTT к данному устройству,
                    // имеют общий префикс /devices/<идентификатор топика>/...
                    "id": "msu34tlp",

                    // идентификатор slave
                    "slave_id": 2,

                    // включить/выключить устройство. В случае задания
                    // "enabled": false опрос устройства и запись значений
                    // его каналов не происходит. По умолчанию - true.
                    "enabled": true,

                    // максимальное количество считываемых "пустых" регистров.
                    // Драйвер в целях оптимизации может считывать регистры
                    // "пачкой". При этом, если какие-либо регистры не
                    // были включены в конфигурацию, но в целях ускорения
                    // опроса (чтобы не разрывать "пачку") их всё-таки
                    // можно считывать, можно указать значение max_hole_size
                    // больше 0. В данный момент поддерживается только
                    // устройствами Modbus.
                    "max_reg_hole": 10,

                    // то же самое, что max_reg_hole, но для однобитовых
                    // регистров (coils и discrete inputs в Modbus). В данный
                    // момент поддерживается только устройствами Modbus.
                    "max_bit_hole": 80,

                    // максимальное количество регистров в одной пакетной операции
                    // чтения. В данный момент поддерживается только устройствами
                    // Modbus.
                    "max_read_registers": 10,

                    // Минимальный интервал опроса регистров данного устройства
                    // по умолчанию, в миллисекундах
                    "poll_interval": 10,

                    // Минимальный интервал между опросом индивидуальных регистров
                    // данного устройства в микросекундах
                    "guard_interval_us": 0,

                    // список каналов устройства
                    "channels": [
                        {
                            // имя канала. topic'и, соответствующие каналу,
                            // публикуются как
                            // /devices/<идентификатор канала>/controls/<имя канала>
                            "name" : "Temp 1",

                            // тип регистра
                            // возможные значения для Modbus:
                            // "coil" - 1 бит, чтение/запись
                            // "discrete" - 1 бит, только чтение
                            // "holding" - 16 бит, чтение/запись
                            // "input" - 16 бит, только чтение
                            "reg_type" : "input",

                            // адрес регистра
                            "address" : 0,

                            // тип элемента управления, например,
                            // "temperature", "text", "switch"
                            // Тип wo-switch задаёт вариант switch,
                            // для которого не производится опрос регистра -
                            // для таких каналов возможна только запись.
                            "type": "temperature",

                            // формат канала. Задаётся для регистров типа
                            // "holding" и "input". Возможные значения:
                            // "u16" - беззнаковое 16-битное целое
                            //         (используется по умолчанию)
                            // "s16" - знаковое 16-битное целое
                            // "u8" - беззнаковое 8-битное целое
                            // "s8" - знаковое 8-битное целое
                            // "u32" - беззнаковое 32-битное целое (big-endian).
                            //     (занимает 2 регистра, начиная с указанного)
                            // "s32" - знаковое 32-битное целое (big-endian).
                            //     (занимает 2 регистра, начиная с указанного)
                            // "s64" - знаковое 64-битное целое (big-endian).
                            //     (занимает 4 регистра, начиная с указанного)
                            // "u64" - беззнаковое 64-битное целое (big-endian).
                            //     (занимает 4 регистра, начиная с указанного)
                            //
                            // "float" - число с плаваяющей точкой IEEE 754. 32 bit. (big-endian).
                            //     (занимает 2 регистра, начиная с указанного)
                            // "double" - число с плаваяющей точкой двойной точности IEEE 754. 64 bit. (big-endian).
                            //     (занимает 4 регистра, начиная с указанного)
                            // "char8" - однобайтовый символ в кодировке ASCII

                            "format": "s8",

                            // Порядок 16-битных слов для каналов, имеющих размер больше 16 бит.
                            // Возможные значения:
                            //  "big_endian" (по-умолчанию): [0xAA 0xBB] [0xCC 0xDD] => 0xAABBCCDD
                            //  "little_endian":  [0xAA 0xBB] [0xCC 0xDD] => 0xCCDDAABB
                            "word_order" : "big_endian",

                            // для регистров типа coil и discrete
                            // с типом отображения switch/wo-swich
                            // также допускается задание on_value -
                            // числового значения, соответствующего
                            // состоянию "on" (см. ниже)

                            // минимальный интервал опроса данного регистра в миллисекундах
                            "poll_interval": 10
                        },
                        {
                            // Ещё один канал
                            "name" : "Illuminance",
                            "reg_type" : "input",
                            "address" : 1,
                            "type": "text"
                        },
                        {
                            "name" : "Pressure",
                            "reg_type" : "input",
                            "address" : 2,
                            "type": "text",
                            "scale": 0.075
                        },
                        {
                            "name" : "Temp 2",
                            "reg_type" : "input",
                            "address" : 3,
                            "type": "temperature",
                            "format": "s8"
                        }
                    ]
                },
                {
                    // ещё одно устройство на канале
                    "name": "DRB88",
                    "id": "drb88",
                    "enabled": true,
                    "slave_id": 22,

                    // секция инициализации
                    "setup": [
                        {
                            // название регистра (для отладки)
                            // Выводится в случае включённой отладочной печати.
                            "title": "Input 0 type",
                            // адрес holding-регистра
                            "address": 1,
                            // значение для записи
                            "value": 1
                        },
                        {
                            "title": "Input 0 module",
                            "address": 3,
                            "value": 3 // was: 11
                        }
                    ],
                    "channels": [
                        {
                            "name" : "Relay 1",
                            "reg_type" : "coil",
                            "address" : 0,
                            "type": "switch"
                        },
                        {
                            "name" : "Relay 2",
                            "reg_type" : "coil",
                            "address" : 1,
                            "type": "switch"
                        },
                        // ...
                        {
                            "name" : "Input 2",
                            "reg_type" : "input",
                            "address" : 1,
                            "type": "switch",
                            // значение, соответствующее состоянию "on"
                            "on_value": 101
                        },
                        {
                            "name" : "Input 3",
                            "reg_type" : "input",
                            "address" : 2,
                            "type": "switch",
                            "on_value": 101
                        },
                        // ...
                    ]
                }
            ]
        },
        {
            // ещё один порт со своим набором устройств
            "path" : "/dev/ttyNSC1",
            "baud_rate": 9600,
            "parity": "N",
            "data_bits": 8,
            "stop_bits": 1,
            "poll_interval": 100,
            "enabled": true,
            "devices" : [
                {
                    "name": "tM-P3R3",
                    "id": "tmp3r3",
                    "enabled": true,
                    "slave_id": 1,
                    "channels": [
                        {
                            "name" : "Relay 0",
                            "reg_type" : "coil",
                            "address" : 0,
                            "type": "switch"
                        },
                        // ...
                    ]
                },
                // ...
            ]
        }
    ]
}
```

Для поддерживаемых устройств существуют шаблоны конфигурации. Для
примера в приведенном ниже файле приведены разные варианты записи
параметров, первое устройство задано через шаблон, второй device через
шаблон, но параметры "name" и "id" заданы, и также можно добавить
конфигурацию для канала, который добавится к тем, что есть в
шаблоне(но он не должен описывать тот же канал что уже есть в
шаблоне), и параметры третьего устройства записаны явно. Шаблоны для
устройств типа "uniel" также находятся в папке
/usr/share/wb-mqtt-serial/templates/

См. также: [пример конфигурационного файла с устройствами Uniel](wb-mqtt-serial/config-uniel.json).

Объединенное чтение регистров и его авто-отключение
---------------------------------------------------

Для ускорения опроса регистров устройств, драйвер объединяет чтение соседних регистров в один запрос (см. max_reg_hole, max_bit_hole),
однако, считывание т.н. "пустых" регистров может привести к ошибкам на некоторых устройствах. Как только драйвер получает от устройства
ошибку при считывании множества регистров, среди которых есть пустые, которая могла быть вызвана чтением пустых регистров
(для Modbus: ILLEGAL_DATA_ADDRESS, ILLEGAL_DATA_VALUE), драйвер перестает объединенно считывать эти регистры.

Поддержка различных протоколов на одной шине
--------------------------------------------

Возможно использование устройств, работающих по различным протоколам,
на одном порту. При этом следует учитывать особенности конкретных
протоколов.  Например, фреймы устройств Uniel начинаются с байта 0xff,
устройств ИВТМ - с байта 0x24 ('$'), в случае же протоколов Modbus,
Меркурий 230 и Милур первым байтом фрейма является идентификатор
slave, поэтому при совмещении подобных устройств следует внимательно
подходить к выбору slave id - у устройств Милур, например, slave id по
умолчанию равен 0xff, что приводит к конфликту с устройствами
Uniel. Устройства Милур требуют дополнительных задержек при опросе
(заданы в шаблоне) и при использовании на одной шине с другими
устройствами могут снизить скорость опроса. Некоторые устройства,
поддерживающие дополнительные протоколы, могут оказаться
несовместимыми с теми или иными протоколами на той же шине, например,
было замечено, что устройства с поддержкой протокола A-BUS
производства "Разумный дом" не могут работать на одной шине с
устройствами Uniel. Работа устройств ИВТМ на одной шине с
устройствами, работающими по другим протоколам, не
проверялась. Проверенная рабочая комбинация: Modbus + Milur (slave_id
!= 0xff) + Uniel на одной шине.

Таблица шаблонов device_type
-------------

Сгруппирована по протоколам.

**Modbus-RTU**

|                          Device                         | device_type | id_prefix  | name_prefix   |
|:-------------------------------------------------------:|:-----------:|:----------:|:-------------:|
| "Разумный дом" четырёхканальный диммер светодиодов DDL4 |    DDL24    |   ddl24    |    DDL24      |
|           "Разумный дом" релейный модуль DRB88          |    DRB88    |   drb88    |    DRB88      |
|      "ICP DAS" модуль управления освещением LC-103      |    LC-103   |   lc-103   |    LC-103     |
|                   "Разумный дом" MSU24                  |    MSU24    |   msu24    |    MSU24      |
|                   "Разумный дом" MSU21                  |    MSU21    |   msu21    |    MSU21      |
|                   "Разумный дом" MSU34+TLP              |    MSU34    |   msu34tlp |  MSU34+TLP    |
|                   "Разумный дом" MSU34+TLHP             | MSU34TLHP   | msu34tlhp  |  MSU34+TLHP   |
|          "ICP DAS" модуль ввода-вывода TM-P3R3          |   TM-P3R3   |  TM-P3R3   |    tmp3r3     |
|     "Kvadro" модуль подключения термометров 1-wire      |kvadro-1wire |kvadro-1wire| Kvadro 1-Wire |
|     PD561Z-9SY счётчик электроэнергии                   |    PD561Z   |  pd561z    | pd561z        |
|     SDM220 счётчик электроэнергии                       |    SDM220   |  sdm220    |  sdm220       |
|     SDM120 счётчик электроэнергии                       |    SDM120   |  sdm120    |  sdm120       |
|     WELLPRO WP8028ADAM (8DI/8DO)                        | WP8028ADAM  | wp8028adam |   WP8028ADAM  |
|  Wiren Board RGB-диммер WB-MRGB                         |   WB-MRGB   |  WB-MRGB   |   wb-mrgb     |
|  Wiren Board Релейный модуль WB-MRM2                    |   WB-MRM2   |  WB-MRM2   |   wb-mrm2     |
|  Wiren Board Релейный модуль WB-MR11                    |   WB-MR11   |  WB-MR11   |   wb-mr11     |
|  Wiren Board Релейный модуль WB-MR14                    |   WB-MR14   |  WB-MR14   |   wb-mr14     |
|  Wiren Board модуль дискретных/счётных входов WB-MCM16  |   WB-MCM16  |  WB-MCM16  |   wb-mcm16    |
|  Wiren Board Датчик WB-MS-THLS / WB-MSW-THLS            |  WB-MS-THLS | wb-ms-thls | WB-MS-THLS    |
|  Wiren Board Датчик WB-MS-THLS / WB-MSW-THLS  (fw. v.2) |  WB-MS-THLS v.2 | wb-ms-thls | WB-MS-THLS    |


**Милур**

|                     Device                     | device_type   | id_prefix | name_prefix      |
|:----------------------------------------------:|:-------------:|:---------:|:----------------:|
| Счётчик электроэнергии Милур-305               | milur305      | milur305  | Milur 305        |
| Счётчик электроэнергии Милур-105 (Милур-104)   | Milur 104/105 | milur105  | Milur 105        |

**Mercury 230**

|                     Device                     | device_type | id_prefix      | name_prefix      |
|:----------------------------------------------:|:-----------:|:--------------:|:----------------:|
| Счётчик электроэнергии Меркурий-230            | mercury230  | mercury230ar02 | Mercury 230AR-02 |

**Uniel**

|                     Device                     | device_type | id_prefix      | name_prefix      |
|:----------------------------------------------:|:-----------:|:--------------:|:----------------:|
| Модуль управления освещением UCH-M111RX        | UCH-M111RX  | uchm111rx      | UCH-M111RX 0808  |
| Модуль управления автоматикой UCH-M121RX       | UCH-M121RX  | uchm121rx      | UCH-M121RX 0808  |
| Диммер светодиодных ламп UCH-M141RC            | UCH-M141RC  | uchm141rc      | UCH-M141RC 0808  |



**ИВТМ**

|                     Device                     | device_type | id_prefix      | name_prefix      |
|:----------------------------------------------:|:-----------:|:--------------:|:----------------:|
| Термогигрометр ИВТМ-7 М 3                      | IVTM-7M-3   | ivtm7m3        | IVTM-7M-3        |


**Пульсар**

|                     Device                     | device_type    | id_prefix      | name_prefix           |
|:----------------------------------------------:|:--------------:|:--------------:|:---------------------:|
| Счётчик воды "Пульсар"                         | pulsar-water   | pulsar-water   |  Pulsar Water Meter   |
| Счётчик воды многоструйный "Пульсар-М"         | pulsar-m-water | pulsar-m-water |  Pulsar-M Water Meter |
| Счётчик тепла "Пульсар"                        | pulsar-heat    | pulsar-heat    |  Pulsar Heat Meter    |
