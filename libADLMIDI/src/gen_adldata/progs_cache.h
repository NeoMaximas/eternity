#ifndef PROGS_H
#define PROGS_H

#include <map>
#include <set>
#include <utility>
#include <memory>
#include <cstring>
#include <cstdint>
#include <vector>

struct insdata
{
    uint8_t         data[11];
    int8_t          finetune;
    bool            diff;
    bool operator==(const insdata &b) const
    {
        return std::memcmp(data, b.data, 11) == 0 && finetune == b.finetune && diff == b.diff;
    }
    bool operator< (const insdata &b) const
    {
        int c = std::memcmp(data, b.data, 11);
        if(c != 0) return c < 0;
        if(finetune != b.finetune) return finetune < b.finetune;
        if(diff != b.diff) return (!diff) == (b.diff);
        return 0;
    }
    bool operator!=(const insdata &b) const
    {
        return !operator==(b);
    }
};

struct ins
{
    size_t insno1, insno2;
    unsigned char notenum;
    bool pseudo4op;
    double fine_tune;

    bool operator==(const ins &b) const
    {
        return notenum == b.notenum
               && insno1 == b.insno1
               && insno2 == b.insno2
               && pseudo4op == b.pseudo4op
               && fine_tune == b.fine_tune;
    }
    bool operator< (const ins &b) const
    {
        if(insno1 != b.insno1) return insno1 < b.insno1;
        if(insno2 != b.insno2) return insno2 < b.insno2;
        if(notenum != b.notenum) return notenum < b.notenum;
        if(pseudo4op != b.pseudo4op) return pseudo4op < b.pseudo4op;
        if(fine_tune != b.fine_tune) return fine_tune < b.fine_tune;
        return 0;
    }
    bool operator!=(const ins &b) const
    {
        return !operator==(b);
    }
};

typedef std::map<insdata, std::pair<size_t, std::set<std::string> > > InstrumentDataTab;
extern InstrumentDataTab insdatatab;

typedef std::map<ins, std::pair<size_t, std::set<std::string> > > InstrumentsData;
extern InstrumentsData instab;

typedef std::map<size_t, std::map<size_t, size_t> > InstProgsData;
extern InstProgsData progs;

extern std::vector<std::string> banknames;

//static std::map<unsigned, std::map<unsigned, unsigned> > Correlate;
//extern unsigned maxvalues[30];

void SetBank(unsigned bank, unsigned patch, size_t insno);

size_t InsertIns(const insdata &id, const insdata &id2, ins &in,
                 const std::string &name, const std::string &name2 = "");
size_t InsertNoSoundIns();

#endif // PROGS_H
