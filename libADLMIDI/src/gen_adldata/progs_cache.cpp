#include "progs_cache.h"

InstrumentDataTab insdatatab;

InstrumentsData instab;
InstProgsData   progs;

std::vector<std::string> banknames;

//unsigned maxvalues[30] = { 0 };

void SetBank(unsigned bank, unsigned patch, size_t insno)
{
    progs[bank][patch] = insno + 1;
}

size_t InsertIns(
    const insdata &id,
    const insdata &id2,
    ins &in,
    const std::string &name,
    const std::string &name2)
{
    if(true)
    {
        InstrumentDataTab::iterator i = insdatatab.lower_bound(id);

        size_t insno = size_t(~0);
        if(i == insdatatab.end() || i->first != id)
        {
            std::pair<insdata, std::pair<size_t, std::set<std::string> > > res;
            res.first = id;
            res.second.first = insdatatab.size();
            if(!name.empty()) res.second.second.insert(name);
            if(!name2.empty()) res.second.second.insert(name2);
            insdatatab.insert(i, res);
            insno = res.second.first;
        }
        else
        {
            if(!name.empty()) i->second.second.insert(name);
            if(!name2.empty()) i->second.second.insert(name2);
            insno = i->second.first;
        }

        in.insno1 = insno;
    }
    if(id != id2)
    {
        InstrumentDataTab::iterator i = insdatatab.lower_bound(id2);

        size_t insno2 = size_t(~0);
        if(i == insdatatab.end() || i->first != id2)
        {
            std::pair<insdata, std::pair<size_t, std::set<std::string> > > res;
            res.first = id2;
            res.second.first = insdatatab.size();
            if(!name.empty()) res.second.second.insert(name);
            if(!name2.empty()) res.second.second.insert(name2);
            insdatatab.insert(i, res);
            insno2 = res.second.first;
        }
        else
        {
            if(!name.empty()) i->second.second.insert(name);
            if(!name2.empty()) i->second.second.insert(name2);
            insno2 = i->second.first;
        }
        in.insno2 = insno2;
    }
    else
        in.insno2 = in.insno1;


    {
        InstrumentsData::iterator i = instab.lower_bound(in);

        size_t resno = size_t(~0);
        if(i == instab.end() || i->first != in)
        {
            std::pair<ins, std::pair<size_t, std::set<std::string> > > res;
            res.first = in;
            res.second.first = instab.size();
            if(!name.empty()) res.second.second.insert(name);
            if(!name2.empty()) res.second.second.insert(name2);
            instab.insert(i, res);
            resno = res.second.first;
        }
        else
        {
            if(!name.empty()) i->second.second.insert(name);
            if(!name2.empty()) i->second.second.insert(name2);
            resno = i->second.first;
        }
        return resno;
    }
}

// Create silent 'nosound' instrument
size_t InsertNoSoundIns()
{
    // { 0x0F70700,0x0F70710, 0xFF,0xFF, 0x0,+0 },
    insdata tmp1 = { {0x00, 0x10, 0x07, 0x07, 0xF7, 0xF7, 0x00, 0x00, 0xFF, 0xFF, 0x00}, 0, 0 };
    struct ins tmp2 = { 0, 0, 0, false, 0.0 };
    return InsertIns(tmp1, tmp1, tmp2, "nosound");
}
