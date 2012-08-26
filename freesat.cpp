/* (c) 2012 Sven Over <svenover@svenover.de>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

#include <iostream>
#include <vector>
#include <inttypes.h>

#include <boost/shared_ptr.hpp>
#include <boost/lexical_cast.hpp>

#include <dvbpsi/dvbpsi.h>
#include <dvbpsi/psi.h>
#include <dvbpsi/demux.h>
#include <dvbpsi/descriptor.h>
#include <dvbpsi/bat.h>
#include <dvbpsi/dr_47.h>

#include <lastjson/value.hpp>
#include <lastjson/stringify.hpp>

#define TS_PACKET_SIZE 188

class tsreader
{
public:
    tsreader(std::istream & str)
    : m_str(str)
    , m_buffer(1024*1024)
    , m_filled(0)
    , m_readpos(0)
    {
    }

    uint8_t const * get_packet()
    {
        while (true)
        {
            while (m_filled - m_readpos < TS_PACKET_SIZE)
            {
                if (m_str.bad() || m_str.eof())
                {
                    return 0;
                }

                if (m_readpos > m_buffer.size()/2)
                {
                    std::copy(&m_buffer[m_readpos], &m_buffer[m_filled], m_buffer.begin());
                    m_filled -= m_readpos;
                    m_readpos = 0;
                }

                m_str.read(&m_buffer[m_filled], m_buffer.size()-m_filled);
                m_filled += m_str.gcount();
            }

            if (m_buffer[m_readpos] != 'G')
            {
                while (m_readpos < m_filled && m_buffer[m_readpos] != 'G')
                {
                    ++m_readpos;
                }
                continue;
            }
            
            uint8_t const * rv = reinterpret_cast<uint8_t const *>(&m_buffer[m_readpos]);
            m_readpos += TS_PACKET_SIZE;

            return rv;
        }
    }
    
private:
    std::istream & m_str;
    std::vector<char> m_buffer;
    size_t m_filled;
    size_t m_readpos;
};

class bat
{
public:
    bat()
    : m_data(lastjson::value::object_type())
    {
    }
    
    static void new_subtable(void * p_arg, dvbpsi_handle h_dvbpsi, uint8_t i_table_id, uint16_t i_extension)
    {
        bat & self = *reinterpret_cast<bat *>(p_arg);
        
        if(i_table_id == 0x4a)
        {
            dvbpsi_AttachBAT(h_dvbpsi, i_table_id, i_extension, &put_bat, reinterpret_cast<void*>(&self));
        }
    }

    lastjson::value const & data() const
    {
        return m_data;
    }
    
private:
    static void put_bat(void* p_arg, dvbpsi_bat_t* p_bat)
    {
        bat & self = *reinterpret_cast<bat *>(p_arg);
        self.put_bat(p_bat);
    }
    void put_bat(dvbpsi_bat_t* p_bat)
    {
        lastjson::value & bouq = m_data[boost::lexical_cast<std::string>(p_bat->i_bouquet_id)];
        try
        {
            if (bouq["version"].get_int() >= p_bat->i_version)
            {
                return;
            }
        }
        catch (lastjson::json_error const &)
        {
        }

        bouq = lastjson::value::object_type();
        bouq["version"] = p_bat->i_version;
        bouq["regions"] = lastjson::value::object_type();
        bouq["services"] = lastjson::value::object_type();

        for (dvbpsi_descriptor_t * desc = p_bat->p_first_descriptor; desc; desc = desc->p_next)
        {
            if (desc->i_tag == 0x47)
            {
                // bouquet_name_descriptor
                dvbpsi_bouquet_name_dr_t * name_desc = dvbpsi_DecodeBouquetNameDr(desc);
                bouq["name"] = std::string(reinterpret_cast<char const *>(name_desc->i_char),
                                           name_desc->i_name_length);
            }
            if (desc->i_tag == 0xd4)
            {
                // region descriptor
                uint8_t * it = desc->p_data;
                uint8_t * end = it + desc->i_length;
                lastjson::value::object_type & regions = bouq["regions"].get_object_ref();
                
                while (end - it >= 6)
                {
                    uint16_t number = it[0]<<8 | it[1];
                    uint8_t length = it[5];

                    if (end - it < 6+length)
                    {
                        break;
                    }
                    
                    regions[boost::lexical_cast<std::string>(number)] = std::string(reinterpret_cast<char const*>(it) + 6, length);

                    it += 6 + length;
                }
            }
        }

        for (dvbpsi_bat_ts_t * ts = p_bat->p_first_ts; ts; ts = ts->p_next)
        {
            uint16_t const ts_id = ts->i_ts_id;
            
            for (dvbpsi_descriptor_t * desc = ts->p_first_descriptor; desc; desc = desc->p_next)
            {
                if (desc->i_tag == 0xd3)
                {
                    // channel number descriptor
                    uint8_t * it = desc->p_data;
                    uint8_t * end = it + desc->i_length;
                    lastjson::value::object_type & services = bouq["services"].get_object_ref();

                    while (end - it >= 5)
                    {
                        uint16_t sid = it[0]<<8 | it[1];
                        uint8_t epg = it[2]<<8 | it[3];
                        uint8_t length = it[4];
                        it += 5;
                        
                        if (end - it < length)
                        {
                            break;
                        }

                        lastjson::value & service = services[boost::lexical_cast<std::string>(sid)];
                        if (service.is_null())
                        {
                            service = lastjson::value::object_type();
                            service["channelnumbers"] = lastjson::value::array_type();
                        }
                        lastjson::value::array_type & channelnumbers = service["channelnumbers"].get_array_ref();

                        std::map<std::string, uint16_t> item;
                        
                        for (size_t idx = 0; idx+3 < length; idx+=4)
                        {
                            item["number"] = (it[idx] << 8 | it[idx+1]) & 0xfff; // number
                            item["region"] = it[idx+2] << 8 | it[idx+3]; // region

                            channelnumbers.push_back(item);
                        }

                        it += length;
                    }
                }
            }
        }
    }

    lastjson::value m_data;
};

using namespace std;

int main(int argc, char *argv[])
{
    if (argc > 1)
    {
        cerr << "Usage: " << argv[0] << endl;
        return 1;
    }

    tsreader reader(cin);
    bat b;
    dvbpsi_handle h_dvbpsi = dvbpsi_AttachDemux(&bat::new_subtable, reinterpret_cast<void*>(&b));
    std::vector<uint8_t> data(188);
    
    while (uint8_t const *p=reader.get_packet())
    {
        uint16_t i_pid = ((uint16_t)(p[1] & 0x1f) << 8) + p[2];
        
        if (i_pid == 3002)
        {
            copy(p, p + TS_PACKET_SIZE, data.begin());
            dvbpsi_PushPacket(h_dvbpsi, &data[0]);
        }
    }
    
    dvbpsi_DetachDemux(h_dvbpsi);

    b.data().write_json(cout);
    cout << endl;
    
    return 0;
}
