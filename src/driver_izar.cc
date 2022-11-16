/*
 Copyright (C) 2019 Jacek Tomasiak (gpl-3.0-or-later)
 Copyright (C) 2020-2022 Fredrik Öhrström (gpl-3.0-or-later)
 Copyright (C) 2021 Vincent Privat (gpl-3.0-or-later)

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include"meters_common_implementation.h"
#include"manufacturer_specificities.h"

namespace
{
    /** Contains all the booleans required to store the alarms of a PRIOS device. */
    typedef struct _izar_alarms {
        bool general_alarm;
        bool leakage_currently;
        bool leakage_previously;
        bool meter_blocked;
        bool back_flow;
        bool underflow;
        bool overflow;
        bool submarine;
        bool sensor_fraud_currently;
        bool sensor_fraud_previously;
        bool mechanical_fraud_currently;
        bool mechanical_fraud_previously;
    } izar_alarms;

    struct Driver : public virtual MeterCommonImplementation
    {
        Driver(MeterInfo &mi, DriverInfo &di);

        void processContent(Telegram *t);

    private:

        double totalWaterConsumption(Unit u);
        string serialNumber();
        double lastMonthTotalWaterConsumption(Unit u);
        string setH0Date();
        string currentAlarmsText();
        string previousAlarmsText();

        vector<uchar> decodePrios(const vector<uchar> &origin, const vector<uchar> &payload, uint32_t key);

        string prefix;
        uint32_t serial_number {0};
        double remaining_battery_life;
        uint16_t h0_year;
        uint8_t h0_month;
        uint8_t h0_day;
        double total_water_consumption_l_ {};
        double last_month_total_water_consumption_l_ {};
        uint32_t transmit_period_s_ {};
        uint16_t manufacture_year {0};
        izar_alarms alarms;

        vector<uint32_t> keys;
    };

    static bool ok = registerDriver([](DriverInfo&di)
    {
        di.setName("izar");
        di.setMeterType(MeterType::WaterMeter);
        di.addLinkMode(LinkMode::T1);
        di.addDetection(MANUFACTURER_HYD,  0x07,  0x85);
        di.addDetection(MANUFACTURER_SAP,  0x15,    -1);
        di.addDetection(MANUFACTURER_SAP,  0x04,    -1);
        di.addDetection(MANUFACTURER_SAP,  0x07,  0x00);
        di.addDetection(MANUFACTURER_DME,  0x07,  0x78);
        di.addDetection(MANUFACTURER_DME,  0x06,  0x78);
        di.addDetection(MANUFACTURER_HYD,  0x07,  0x86);

        di.setConstructor([](MeterInfo& mi, DriverInfo& di){ return shared_ptr<Meter>(new Driver(mi, di)); });
    });

    Driver::Driver(MeterInfo &mi, DriverInfo &di) : MeterCommonImplementation(mi, di)
    {
        initializeDiehlDefaultKeySupport(meterKeys()->confidentiality_key, keys);

        addPrint("prefix", Quantity::Text,
                 [&](){ return prefix; },
                 "The alphanumeric prefix printed before serial number on device.",
                 PrintProperty::FIELD | PrintProperty::JSON);

        addPrint("serial_number", Quantity::Text,
                 [&](){ return serialNumber(); },
                 "The meter serial number.",
                 PrintProperty::FIELD | PrintProperty::JSON);

        addPrint("total", Quantity::Volume,
                 [&](Unit u){ return totalWaterConsumption(u); },
                 "The total water consumption recorded by this meter.",
                 PrintProperty::FIELD | PrintProperty::JSON);

        addPrint("last_month_total", Quantity::Volume,
                 [&](Unit u){ return lastMonthTotalWaterConsumption(u); },
                 "The total water consumption recorded by this meter around end of last month.",
                 PrintProperty::FIELD | PrintProperty::JSON);

        addPrint("last_month_measure_date", Quantity::Text,
                 [&](){ return setH0Date(); },
                 "The date when the meter recorded the most recent billing value.",
                 PrintProperty::FIELD | PrintProperty::JSON);

        addPrint("remaining_battery_life", Quantity::Time, Unit::Year,
                 [&](Unit u){ return convert(remaining_battery_life, Unit::Year, u); },
                 "How many more years the battery is expected to last",
                 PrintProperty::FIELD | PrintProperty::JSON);

        addPrint("current_alarms", Quantity::Text,
                 [&](){ return currentAlarmsText(); },
                 "Alarms currently reported by the meter.",
                 PrintProperty::FIELD | PrintProperty::JSON);

        addPrint("previous_alarms", Quantity::Text,
                 [&](){ return previousAlarmsText(); },
                 "Alarms previously reported by the meter.",
                 PrintProperty::FIELD | PrintProperty::JSON);

        addPrint("transmit_period", Quantity::Time, Unit::Second,
                 [&](Unit u){ return convert(transmit_period_s_, Unit::Second, u); },
                 "The period at which the meter transmits its data.",
                 PrintProperty::FIELD | PrintProperty::JSON);

        addPrint("manufacture_year", Quantity::Text,
                 [&](){ return to_string(manufacture_year); },
                 "The year during which the meter was manufactured.",
                 PrintProperty::FIELD | PrintProperty::JSON);
    }

    double Driver::totalWaterConsumption(Unit u)
    {
        assertQuantity(u, Quantity::Volume);
        return convert(total_water_consumption_l_, Unit::L, u);
    }

    double Driver::lastMonthTotalWaterConsumption(Unit u)
    {
        assertQuantity(u, Quantity::Volume);
        return convert(last_month_total_water_consumption_l_, Unit::L, u);
    }

    string Driver::setH0Date()
    {
        string date;
        strprintf(&date, "%d-%02d-%02d", h0_year, h0_month%99, h0_day%99);
        return date;
    }

    string Driver::serialNumber()
    {
        string result;
        strprintf(&result, "%06d", serial_number);
        return result;
    }

    string Driver::currentAlarmsText()
    {
        string s;
        if (alarms.leakage_currently) {
            s.append("leakage,");
        }
        if (alarms.meter_blocked) {
            s.append("meter_blocked,");
        }
        if (alarms.back_flow) {
            s.append("back_flow,");
        }
        if (alarms.underflow) {
            s.append("underflow,");
        }
        if (alarms.overflow) {
            s.append("overflow,");
        }
        if (alarms.submarine) {
            s.append("submarine,");
        }
        if (alarms.sensor_fraud_currently) {
            s.append("sensor_fraud,");
        }
        if (alarms.mechanical_fraud_currently) {
            s.append("mechanical_fraud,");
        }
        if (s.length() > 0) {
            if (alarms.general_alarm) {
                return "general_alarm";
            }
            s.pop_back();
            return s;
        }
        return "no_alarm";
    }

    string Driver::previousAlarmsText()
    {
        string s;
        if (alarms.leakage_previously) {
            s.append("leakage,");
        }
        if (alarms.sensor_fraud_previously) {
            s.append("sensor_fraud,");
        }
        if (alarms.mechanical_fraud_previously) {
            s.append("mechanical_fraud,");
        }
        if (s.length() > 0) {
            s.pop_back();
            return s;
        }
        return "no_alarm";
    }

    void Driver::processContent(Telegram *t)
    {
        vector<uchar> frame;
        t->extractFrame(&frame);
        vector<uchar> origin = t->original.empty() ? frame : t->original;

        vector<uchar> decoded_content;
        for (auto& key : keys) {
            decoded_content = decodePrios(origin, frame, key);
            if (!decoded_content.empty())
                break;
        }

        debug("(izar) Decoded PRIOS data: %s\n", bin2hex(decoded_content).c_str());

        if (decoded_content.empty())
        {
            if (t->beingAnalyzed() == false)
            {
                warning("(izar) Decoding PRIOS data failed. Ignoring telegram.\n");
            }
            return;
        }

        if (detectDiehlFrameInterpretation(frame) == DiehlFrameInterpretation::SAP_PRIOS)
        {
            string digits = to_string((origin[7] & 0x03) << 24 | origin[6] << 16 | origin[5] << 8 | origin[4]);
            // get the manufacture year
            uint8_t yy = atoi(digits.substr(0, 2).c_str());
            manufacture_year = yy > 70 ? (1900 + yy) : (2000 + yy); // Maybe to adjust in 2070, if this code stills lives :D
            // get the serial number
            serial_number = atoi(digits.substr(2, digits.size()).c_str());
            // get letters
            uchar supplier_code = '@' + (((origin[9] & 0x0F) << 1) | (origin[8] >> 7));
            uchar meter_type = '@' + ((origin[8] & 0x7C) >> 2);
            uchar diameter = '@' + (((origin[8] & 0x03) << 3) | (origin[7] >> 5));
            // build the prefix
            strprintf(&prefix, "%c%02d%c%c", supplier_code, yy, meter_type, diameter);
        }

        // get the remaining battery life (in year) and transmission period (in seconds)
        remaining_battery_life = (frame[12] & 0x1F) / 2.0;
        transmit_period_s_ = 1 << ((frame[11] & 0x0F) + 2);

        total_water_consumption_l_ = uint32FromBytes(decoded_content, 1, true);
        last_month_total_water_consumption_l_ = uint32FromBytes(decoded_content, 5, true);

        // get the date when the second measurement was taken
        h0_year = ((decoded_content[10] & 0xF0) >> 1) + ((decoded_content[9] & 0xE0) >> 5);
        if (h0_year > 80) {
            h0_year += 1900;
        } else {
            h0_year += 2000;
        }
        h0_month = decoded_content[10] & 0xF;
        h0_day = decoded_content[9] & 0x1F;

        // read the alarms:
        alarms.general_alarm = frame[11] >> 7;
        alarms.leakage_currently = frame[12] >> 7;
        alarms.leakage_previously = frame[12] >> 6 & 0x1;
        alarms.meter_blocked = frame[12] >> 5 & 0x1;
        alarms.back_flow = frame[13] >> 7;
        alarms.underflow = frame[13] >> 6 & 0x1;
        alarms.overflow = frame[13] >> 5 & 0x1;
        alarms.submarine = frame[13] >> 4 & 0x1;
        alarms.sensor_fraud_currently = frame[13] >> 3 & 0x1;
        alarms.sensor_fraud_previously = frame[13] >> 2 & 0x1;
        alarms.mechanical_fraud_currently = frame[13] >> 1 & 0x1;
        alarms.mechanical_fraud_previously = frame[13] & 0x1;
    }

    vector<uchar> Driver::decodePrios(const vector<uchar> &origin, const vector<uchar> &frame, uint32_t key)
    {
        return decodeDiehlLfsr(origin, frame, key, DiehlLfsrCheckMethod::HEADER_1_BYTE, 0x4B);
    }
}

// Test: IzarWater izar 21242472 NOKEY
// telegram=|1944304C72242421D401A2|013D4013DD8B46A4999C1293E582CC|
// {"media":"water","meter":"izar","name":"IzarWater","id":"21242472","prefix":"C19UA","serial_number":"145842","total_m3":3.488,"last_month_total_m3":3.486,"last_month_measure_date":"2019-09-30","remaining_battery_life_y":14.5,"current_alarms":"meter_blocked,underflow","previous_alarms":"no_alarm","transmit_period_s":8,"manufacture_year":"2019","timestamp":"1111-11-11T11:11:11Z"}
// |IzarWater;21242472;C19UA;145842;3.488000;3.486000;2019-09-30;14.500000;meter_blocked,underflow;no_alarm;8.000000;2019;1111-11-11 11:11.11

// Test: IzarWater2 izar 66236629 NOKEY
// telegram=|2944A511780729662366A20118001378D3B3DB8CEDD77731F25832AAF3DA8CADF9774EA673172E8C61F2|
// {"media":"water","meter":"izar","name":"IzarWater2","id":"66236629","prefix":"","serial_number":"000000","total_m3":16.76,"last_month_total_m3":11.84,"last_month_measure_date":"2019-11-30","remaining_battery_life_y":12,"current_alarms":"no_alarm","previous_alarms":"no_alarm","transmit_period_s":8,"manufacture_year":"0","timestamp":"1111-11-11T11:11:11Z"}
// |IzarWater2;66236629;;000000;16.760000;11.840000;2019-11-30;12.000000;no_alarm;no_alarm;8.000000;0;1111-11-11 11:11.11

// Test: IzarWater3 izar 20481979 NOKEY
// telegram=|1944A511780779194820A1|21170013355F8EDB2D03C6912B1E37
// {"media":"water","meter":"izar","name":"IzarWater3","id":"20481979","prefix":"","serial_number":"000000","total_m3":4.366,"last_month_total_m3":0,"last_month_measure_date":"2020-12-31","remaining_battery_life_y":11.5,"current_alarms":"no_alarm","previous_alarms":"no_alarm","transmit_period_s":8,"manufacture_year":"0","timestamp":"1111-11-11T11:11:11Z"}
// |IzarWater3;20481979;;000000;4.366000;0.000000;2020-12-31;11.500000;no_alarm;no_alarm;8.000000;0;1111-11-11 11:11.11

// Test: IzarWater4 izar 2124589c NOKEY
// Comment: With mfct specific tpl ci field a3.
// telegram=|1944304c9c5824210c04a363140013716577ec59e8663ab0d31c|
// {"media":"water","meter":"izar","name":"IzarWater4","id":"2124589c","prefix":"H19CA","serial_number":"159196","total_m3":38.944,"last_month_total_m3":38.691,"last_month_measure_date":"2021-02-01","remaining_battery_life_y":10,"current_alarms":"no_alarm","previous_alarms":"no_alarm","transmit_period_s":32,"manufacture_year":"2019","timestamp":"1111-11-11T11:11:11Z"}
// |IzarWater4;2124589c;H19CA;159196;38.944000;38.691000;2021-02-01;10.000000;no_alarm;no_alarm;32.000000;2019;1111-11-11 11:11.11

// Test: IzarWater5 izar 20e4ffde NOKEY
// Comment: Ensure non-regression on manufacture year parsing
// telegram=|1944304CDEFFE420CC01A2|63120013258F907B0AFF12529AC33B|
// {"media":"water","meter":"izar","name":"IzarWater5","id":"20e4ffde","prefix":"C15SA","serial_number":"007710","total_m3":159.832,"last_month_total_m3":157.76,"last_month_measure_date":"2021-02-01","remaining_battery_life_y":9,"current_alarms":"no_alarm","previous_alarms":"no_alarm","transmit_period_s":32,"manufacture_year":"2015","timestamp":"1111-11-11T11:11:11Z"}
// |IzarWater5;20e4ffde;C15SA;007710;159.832000;157.760000;2021-02-01;9.000000;no_alarm;no_alarm;32.000000;2015;1111-11-11 11:11.11

// Test: IzarWater6 izar 48500375 NOKEY
// telegram=|19442423860775035048A251520015BEB6B2E1ED623A18FC74A5|
// {"media":"water","meter":"izar","name":"IzarWater6","id":"48500375","prefix":"","serial_number":"000000","total_m3":521.602,"last_month_total_m3":519.147,"last_month_measure_date":"2021-11-15","remaining_battery_life_y":9,"current_alarms":"no_alarm","previous_alarms":"leakage","transmit_period_s":8,"manufacture_year":"0","timestamp":"1111-11-11T11:11:11Z"}
// |IzarWater6;48500375;;000000;521.602000;519.147000;2021-11-15;9.000000;no_alarm;leakage;8.000000;0;1111-11-11 11:11.11
