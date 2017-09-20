//#ifdef __MINGW32__
//typedef struct vswprintf {} swprintf;
//#endif
#include <cstdio>
#include <string>
#include <cstring>

#include "ini/ini_processing.h"

#include "progs_cache.h"
#include "measurer.h"

#include "midi_inst_list.h"

#include "file_formats/load_ail.h"
#include "file_formats/load_bisqwit.h"
#include "file_formats/load_bnk2.h"
#include "file_formats/load_bnk.h"
#include "file_formats/load_ibk.h"
#include "file_formats/load_jv.h"
#include "file_formats/load_op2.h"
#include "file_formats/load_tmb.h"

int main(int argc, char**argv)
{
    if(argc == 1)
    {
        printf("Usage:\n"
               "\n"
               "bin/gen_adldata src/adldata.cpp\n"
               "\n");
        return 1;
    }

    const char *outFile_s = argv[1];

    FILE *outFile = fopen(outFile_s, "w");
    if(!outFile)
    {
        fprintf(stderr, "Can't open %s file for write!\n", outFile_s);
        return 1;
    }

    fprintf(outFile, "\
#include \"adldata.hh\"\n\
\n\
/* THIS OPL-3 FM INSTRUMENT DATA IS AUTOMATICALLY GENERATED\n\
 * FROM A NUMBER OF SOURCES, MOSTLY PC GAMES.\n\
 * PREPROCESSED, CONVERTED, AND POSTPROCESSED OFF-SCREEN.\n\
 */\n\
");
    {
        IniProcessing ini;
        if(!ini.open("banks.ini"))
        {
            fprintf(stderr, "Can't open banks.ini!\n");
            return 1;
        }

        uint32_t banks_count;
        ini.beginGroup("General");
        ini.read("banks", banks_count, 0);
        ini.endGroup();

        if(!banks_count)
        {
            fprintf(stderr, "Zero count of banks found in banks.ini!\n");
            return 1;
        }

        for(uint32_t bank = 0; bank < banks_count; bank++)
        {
            if(!ini.beginGroup(std::string("bank-") + std::to_string(bank)))
            {
                fprintf(stderr, "Failed to find bank %u!\n", bank);
                return 1;
            }
            std::string bank_name;
            std::string filepath;
            std::string filepath_d;
            std::string prefix;
            std::string prefix_d;
            std::string filter_m;
            std::string filter_p;
            std::string format;

            ini.read("name",     bank_name, "Untitled");
            ini.read("format",   format,    "Unknown");
            ini.read("file",     filepath,  "");
            ini.read("file-p",   filepath_d,  "");
            ini.read("prefix",   prefix, "");
            ini.read("prefix-p", prefix_d, "");
            ini.read("filter-m", filter_m, "");
            ini.read("filter-p", filter_p, "");

            if(filepath.empty())
            {
                fprintf(stderr, "Failed to load bank %u, file is empty!\n", bank);
                return 1;
            }

            banknames.push_back(bank_name);

            //printf("Loading %s...\n", filepath.c_str());

            if(format == "AIL")
            {
                if(!LoadMiles(filepath.c_str(), bank, prefix.c_str()))
                {
                    fprintf(stderr, "Failed to load bank %u, file %s!\n", bank, filepath.c_str());
                    return 1;
                }
            }
            else
            if(format == "Bisqwit")
            {
                if(!LoadBisqwit(filepath.c_str(), bank, prefix.c_str()))
                {
                    fprintf(stderr, "Failed to load bank %u, file %s!\n", bank, filepath.c_str());
                    return 1;
                }
            }
            else
            if(format == "OP2")
            {
                if(!LoadDoom(filepath.c_str(), bank, prefix.c_str()))
                {
                    fprintf(stderr, "Failed to load bank %u, file %s!\n", bank, filepath.c_str());
                    return 1;
                }
            }
            else
            if(format == "TMB")
            {
                if(!LoadTMB(filepath.c_str(), bank, prefix.c_str()))
                {
                    fprintf(stderr, "Failed to load bank %u, file %s!\n", bank, filepath.c_str());
                    return 1;
                }
            }
            else
            if(format == "Junglevision")
            {
                if(!LoadJunglevision(filepath.c_str(), bank, prefix.c_str()))
                {
                    fprintf(stderr, "Failed to load bank %u, file %s!\n", bank, filepath.c_str());
                    return 1;
                }
            }
            else
            if(format == "AdLibGold")
            {
                if(!LoadBNK2(filepath.c_str(), bank, prefix.c_str(), filter_m, filter_p))
                {
                    fprintf(stderr, "Failed to load bank %u, file %s!\n", bank, filepath.c_str());
                    return 1;
                }
            }
            else
            if(format == "HMI")
            {
                if(!LoadBNK(filepath.c_str(),  bank, prefix.c_str(),   false, false))
                {
                    fprintf(stderr, "Failed to load bank %u, file %s!\n", bank, filepath.c_str());
                    return 1;
                }
                if(!filepath_d.empty())
                {
                    if(!LoadBNK(filepath_d.c_str(),bank, prefix_d.c_str(), false, true))
                    {
                        fprintf(stderr, "Failed to load bank %u, file %s!\n", bank, filepath.c_str());
                        return 1;
                    }
                }
            }
            else
            if(format == "IBK")
            {
                if(!LoadIBK(filepath.c_str(),  bank, prefix.c_str(),   false))
                {
                    fprintf(stderr, "Failed to load bank %u, file %s!\n", bank, filepath.c_str());
                    return 1;
                }
                if(!filepath_d.empty())
                {
                    //printf("Loading %s... \n", filepath_d.c_str());
                    if(!LoadIBK(filepath_d.c_str(),bank, prefix_d.c_str(), true))
                    {
                        fprintf(stderr, "Failed to load bank %u, file %s!\n", bank, filepath.c_str());
                        return 1;
                    }
                }
            }
            else
            {
                fprintf(stderr, "Failed to load bank %u, file %s!\nUnknown format type %s\n",
                        bank,
                        filepath.c_str(),
                        format.c_str());
                return 1;
            }


            ini.endGroup();
        }

        printf("Loaded %u banks!\n", banks_count);
        fflush(stdout);
    }

    #if 0
    for(unsigned a = 0; a < 36 * 8; ++a)
    {
        if((1 << (a % 8)) > maxvalues[a / 8]) continue;

        const std::map<unsigned, unsigned> &data = Correlate[a];
        if(data.empty()) continue;
        std::vector< std::pair<unsigned, unsigned> > correlations;
        for(std::map<unsigned, unsigned>::const_iterator
            i = data.begin();
            i != data.end();
            ++i)
        {
            correlations.push_back(std::make_pair(i->second, i->first));
        }
        std::sort(correlations.begin(), correlations.end());
        fprintf(outFile, "Byte %2u bit %u=mask %02X:\n", a / 8, a % 8, 1 << (a % 8));
        for(size_t c = 0; c < correlations.size() && c < 10; ++c)
        {
            unsigned count = correlations[correlations.size() - 1 - c ].first;
            unsigned index = correlations[correlations.size() - 1 - c ].second;
            fprintf(outFile, "\tAdldata index %u, bit %u=mask %02X (%u matches)\n",
                    index / 8, index % 8, 1 << (index % 8), count);
        }
    }
    #endif

    printf("Writing raw instrument data...\n");
    fflush(stdout);
    {
        fprintf(outFile,
                /*
                "static const struct\n"
                "{\n"
                "    unsigned modulator_E862, carrier_E862;  // See below\n"
                "    unsigned char modulator_40, carrier_40; // KSL/attenuation settings\n"
                "    unsigned char feedconn; // Feedback/connection bits for the channel\n"
                "    signed char finetune;   // Finetune\n"
                "} adl[] =\n"*/
                "const adldata adl[%u] =\n"
                "{ //    ,---------+-------- Wave select settings\n"
                "  //    | ,-------ч-+------ Sustain/release rates\n"
                "  //    | | ,-----ч-ч-+---- Attack/decay rates\n"
                "  //    | | | ,---ч-ч-ч-+-- AM/VIB/EG/KSR/Multiple bits\n"
                "  //    | | | |   | | | |\n"
                "  //    | | | |   | | | |     ,----+-- KSL/attenuation settings\n"
                "  //    | | | |   | | | |     |    |    ,----- Feedback/connection bits\n"
                "  //    | | | |   | | | |     |    |    |    ,----- Fine tune\n\n"
                "  //    | | | |   | | | |     |    |    |    |\n"
                "  //    | | | |   | | | |     |    |    |    |\n", (unsigned)insdatatab.size());

        for(size_t b = insdatatab.size(), c = 0; c < b; ++c)
        {
            for(std::map<insdata, std::pair<size_t, std::set<std::string> > >
                ::const_iterator
                i = insdatatab.begin();
                i != insdatatab.end();
                ++i)
            {
                if(i->second.first != c) continue;
                fprintf(outFile, "    { ");

                uint32_t carrier_E862 =
                    uint32_t(i->first.data[6] << 24)
                    + uint32_t(i->first.data[4] << 16)
                    + uint32_t(i->first.data[2] << 8)
                    + uint32_t(i->first.data[0] << 0);
                uint32_t modulator_E862 =
                    uint32_t(i->first.data[7] << 24)
                    + uint32_t(i->first.data[5] << 16)
                    + uint32_t(i->first.data[3] << 8)
                    + uint32_t(i->first.data[1] << 0);

                fprintf(outFile, "0x%07X,0x%07X, 0x%02X,0x%02X, 0x%X, %+d",
                        carrier_E862,
                        modulator_E862,
                        i->first.data[8],
                        i->first.data[9],
                        i->first.data[10],
                        i->first.finetune);

                std::string names;
                for(std::set<std::string>::const_iterator
                    j = i->second.second.begin();
                    j != i->second.second.end();
                    ++j)
                {
                    if(!names.empty()) names += "; ";
                    if((*j)[0] == '\377')
                        names += j->substr(1);
                    else
                        names += *j;
                }
                fprintf(outFile, " }, // %u: %s\n", (unsigned)c, names.c_str());
            }
        }
        fprintf(outFile, "};\n");
    }

    /*fprintf(outFile, "static const struct\n"
           "{\n"
           "    unsigned short adlno1, adlno2;\n"
           "    unsigned char tone;\n"
           "    unsigned char flags;\n"
           "    long ms_sound_kon;  // Number of milliseconds it produces sound;\n"
           "    long ms_sound_koff;\n"
           "} adlins[] =\n");*/

    fprintf(outFile, "const struct adlinsdata adlins[%u] =\n", (unsigned)instab.size());
    fprintf(outFile, "{\n");

    MeasureThreaded measureCounter;
    {
        printf("Beginning to generate measures data...\n");
        fflush(stdout);
        measureCounter.LoadCache("fm_banks/adldata-cache.dat");
        measureCounter.m_total = instab.size();
        for(size_t b = instab.size(), c = 0; c < b; ++c)
        {
            for(std::map<ins, std::pair<size_t, std::set<std::string> > >::const_iterator i =  instab.begin(); i != instab.end(); ++i)
            {
                if(i->second.first != c) continue;
                measureCounter.run(i);
            }
        }
        fflush(stdout);
        measureCounter.waitAll();
        measureCounter.SaveCache("fm_banks/adldata-cache.dat");
    }

    printf("Writing generated measure data...\n");
    fflush(stdout);

    std::vector<unsigned> adlins_flags;

    for(size_t b = instab.size(), c = 0; c < b; ++c)
        for(std::map<ins, std::pair<size_t, std::set<std::string> > >
            ::const_iterator
            i = instab.begin();
            i != instab.end();
            ++i)
        {
            if(i->second.first != c) continue;
            //DurationInfo info = MeasureDurations(i->first);
            MeasureThreaded::DurationInfoCache::iterator indo_i = measureCounter.m_durationInfo.find(i->first);
            DurationInfo info = indo_i->second;
            {
                if(info.peak_amplitude_time == 0)
                {
                    fprintf(outFile,
                        "    // Amplitude begins at %6.1f,\n"
                        "    // fades to 20%% at %.1fs, keyoff fades to 20%% in %.1fs.\n",
                        info.begin_amplitude,
                        info.quarter_amplitude_time / double(info.interval),
                        info.keyoff_out_time / double(info.interval));
                }
                else
                {
                    fprintf(outFile,
                        "    // Amplitude begins at %6.1f, peaks %6.1f at %.1fs,\n"
                        "    // fades to 20%% at %.1fs, keyoff fades to 20%% in %.1fs.\n",
                        info.begin_amplitude,
                        info.peak_amplitude_value,
                        info.peak_amplitude_time / double(info.interval),
                        info.quarter_amplitude_time / double(info.interval),
                        info.keyoff_out_time / double(info.interval));
                }
            }

            unsigned flags = (i->first.pseudo4op ? 1 : 0) | (info.nosound ? 2 : 0);

            fprintf(outFile, "    {");
            fprintf(outFile, "%4d,%4d,%3d, %d, %6ld,%6ld,%lf",
                    (unsigned) i->first.insno1,
                    (unsigned) i->first.insno2,
                    (int)(i->first.notenum),
                    flags,
                    info.ms_sound_kon,
                    info.ms_sound_koff,
                    i->first.fine_tune);
            std::string names;
            for(std::set<std::string>::const_iterator
                j = i->second.second.begin();
                j != i->second.second.end();
                ++j)
            {
                if(!names.empty()) names += "; ";
                if((*j)[0] == '\377')
                    names += j->substr(1);
                else
                    names += *j;
            }
            fprintf(outFile, " }, // %u: %s\n\n", (unsigned)c, names.c_str());
            fflush(outFile);
            adlins_flags.push_back(flags);
        }
    fprintf(outFile, "};\n\n");


    printf("Writing banks data...\n");
    fflush(stdout);

    //fprintf(outFile, "static const unsigned short banks[][256] =\n");
    #ifdef HARD_BANKS
    const unsigned bankcount = sizeof(banknames) / sizeof(*banknames);
    #else
    const size_t bankcount = banknames.size();
    #endif

    size_t nosound = InsertNoSoundIns();

    std::map<size_t, std::vector<size_t> > bank_data;
    for(size_t bank = 0; bank < bankcount; ++bank)
    {
        //bool redundant = true;
        std::vector<size_t> data(256);
        for(size_t p = 0; p < 256; ++p)
        {
            size_t v = progs[bank][p];
            if(v == 0 || (adlins_flags[v - 1] & 2))
                v = nosound; // Blank.in
            else
                v -= 1;
            data[p] = v;
        }
        bank_data[bank] = data;
    }
    std::set<size_t> listed;

    fprintf(outFile,
            "\n\n//Returns total number of generated banks\n"
            "int  maxAdlBanks()\n"
            "{"
            "   return %u;\n"
            "}\n\n"
            "const char* const banknames[%u] =\n", (unsigned int)bankcount, (unsigned int)bankcount);
    fprintf(outFile, "{\n");
    for(unsigned bank = 0; bank < bankcount; ++bank)
        fprintf(outFile, "    \"%s\",\n", banknames[bank].c_str());
    fprintf(outFile, "};\n");

    fprintf(outFile, "const unsigned short banks[%u][256] =\n", (unsigned int)bankcount);
    fprintf(outFile, "{\n");
    for(unsigned bank = 0; bank < bankcount; ++bank)
    {
        fprintf(outFile, "    { // bank %u, %s\n", bank, banknames[bank].c_str());
        bool redundant = true;
        for(unsigned p = 0; p < 256; ++p)
        {
            size_t v = bank_data[bank][p];
            if(listed.find(v) == listed.end())
            {
                listed.insert(v);
                redundant = false;
            }
            fprintf(outFile, "%4d,", (unsigned int)v);
            if(p % 16 == 15) fprintf(outFile, "\n");
        }
        fprintf(outFile, "    },\n");
        if(redundant)
        {
            fprintf(outFile, "    // Bank %u defines nothing new.\n", bank);
            for(unsigned refbank = 0; refbank < bank; ++refbank)
            {
                bool match = true;
                for(unsigned p = 0; p < 256; ++p)
                    if(bank_data[bank][p] != nosound
                       && bank_data[bank][p] != bank_data[refbank][p])
                    {
                        match = false;
                        break;
                    }
                if(match)
                    fprintf(outFile, "    // Bank %u is just a subset of bank %u!\n",
                            bank, refbank);
            }
        }
    }

    fprintf(outFile, "};\n");
    fflush(outFile);
    fclose(outFile);

    printf("Generation of ADLMIDI data has been completed!\n");
    fflush(stdout);
}
