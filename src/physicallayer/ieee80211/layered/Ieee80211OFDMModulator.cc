//
// Copyright (C) 2014 OpenSim Ltd.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, see <http://www.gnu.org/licenses/>.
//

#include "Ieee80211OFDMModulator.h"
#include "QAM16Modulation.h"
#include "QAM64Modulation.h"
#include "BPSKModulation.h"
#include "QPSKModulation.h"

namespace inet {
namespace physicallayer {

#define OFDM_SYMBOL_SIZE 48

void Ieee80211OFDMModulator::initialize(int stage)
{
    if (stage == INITSTAGE_LOCAL)
    {
        const char *modulationSchemeStr = par("modulationScheme");
        if (!strcmp("QAM-16", modulationSchemeStr))
            modulationScheme = &QAM16Modulation::singleton;
        else if (!strcmp("QAM-64", modulationSchemeStr))
            modulationScheme = &QAM64Modulation::singleton;
        else if (!strcmp("QPSK", modulationSchemeStr))
            modulationScheme = &QPSKModulation::singleton;
        else if (!strcmp("BPSK", modulationSchemeStr))
            modulationScheme = &BPSKModulation::singleton;
        else
            throw cRuntimeError("Unknown modulation scheme = %s", modulationSchemeStr);
        signalModulationScheme = &BPSKModulation::singleton;
    }
}

int Ieee80211OFDMModulator::getSubcarrierIndex(int ofdmSymbolIndex) const
{
    // This is the translated version of the M(k) function defined in 18.3.5.10 OFDM modulation: (18-23) equation.
    // We translate it by 26 since its range is [-26,26] and we use subcarrier indices as array indices.
    if (0 >= ofdmSymbolIndex && ofdmSymbolIndex <= 4)
        return ofdmSymbolIndex;
    else if (ofdmSymbolIndex >= 5 && ofdmSymbolIndex <= 17)
        return ofdmSymbolIndex + 1;
    else if (ofdmSymbolIndex >= 18 && ofdmSymbolIndex <= 23)
        return ofdmSymbolIndex + 2;
    else if (ofdmSymbolIndex >= 24 && ofdmSymbolIndex <= 29)
        return ofdmSymbolIndex + 3;
    else if (ofdmSymbolIndex >= 30 && ofdmSymbolIndex <= 42)
        return ofdmSymbolIndex + 4;
    else if (ofdmSymbolIndex >= 43 && ofdmSymbolIndex <= 47)
        return ofdmSymbolIndex + 5;
    else
        throw cRuntimeError("The domain of the M(k) (k = %d) function is [0,47]", ofdmSymbolIndex);
}

void Ieee80211OFDMModulator::modulateSignalField(const BitVector& signalField, std::vector<ISymbol> *ofdmSymbols) const
{
    // BPSK modulation of the SIGNAL field
    ASSERT(signalField.getSize() == OFDM_SYMBOL_SIZE);
    OFDMSymbol signalFieldSymbol;
    for (unsigned int i = 0; i < signalField.getSize(); i++)
    {
        ShortBitVector bit;
        bit.appendBit(signalField.getBit(i));
        const APSKSymbol *apskSymbol = signalModulationScheme->mapToConstellationDiagram(bit);
        int subcarrierIndex = getSubcarrierIndex(i);
        signalFieldSymbol.pushAPSKSymbol(apskSymbol, subcarrierIndex);
    }
    ofdmSymbols->push_back(signalFieldSymbol);
}

void Ieee80211OFDMModulator::modulateDataField(const BitVector& dataField, std::vector<ISymbol> *ofdmSymbols) const
{
    // Divide the resulting coded and interleaved data string into groups of N_BPSC bits.
    const int nBPSC = modulationScheme->getCodeWordLength();
// TODO:    const int symbolLength = preambleSymbolLength + (bitModel->getBitLength() + nBPSC - 1) / nBPSC;
//    const double symbolRate = bitModel->getBitRate() / nBPSC;
    ShortBitVector bitGroup;
    std::vector<const APSKSymbol *> apskSymbols;
    for (unsigned int i = 0; i < dataField.getSize(); i++)
    {
        // For each of the bit groups, convert the bit group into a complex number according
        // to the modulation encoding tables
        bitGroup.setBit(i % nBPSC, dataField.getBit(i));
        if (i % nBPSC == nBPSC - 1)
        {
           const APSKSymbol *apskSymbol = modulationScheme->mapToConstellationDiagram(bitGroup);
           apskSymbols.push_back(apskSymbol);
        }
    }
    // Divide the complex number string into groups of 48 complex numbers.
    // Each such group is associated with one OFDM symbol.
    OFDMSymbol ofdmSymbol;
    for (unsigned int i = 0; i < apskSymbols.size(); i++)
    {
        int subcarrierIndex = getSubcarrierIndex(i % OFDM_SYMBOL_SIZE);
        ofdmSymbol.pushAPSKSymbol(apskSymbols.at(i), subcarrierIndex);
        // In each group, the complex numbers are numbered 0 to 47 and mapped hereafter into OFDM
        // subcarriers numbered -26 to -22, -20 to -8, -6 to -1, 1 to 6, 8 to 20, and 22 to 26.
        // The 0 subcarrier, associated with center frequency, is omitted and filled with the value 0.
        if (i % OFDM_SYMBOL_SIZE == OFDM_SYMBOL_SIZE - 1)
        {
            ofdmSymbols->push_back(ofdmSymbol);
            ofdmSymbol.clearSymbols();
        }
    }
    // TODO: Four subcarriers are inserted as pilots into positions -21, -7, 7, and 21. The total number of
    // the subcarriers is 52 (48 + 4).

    // TODO: For each group of subcarriers -26 to 26, convert the subcarriers to time domain using inverse
    // Fourier transform. Prepend to the Fourier-transformed waveform a circular extension of itself thus
    // forming a GI, and truncate the resulting periodic waveform to a single OFDM symbol length by
    // applying time domain windowing.

    // TODO: Append the OFDM symbols one after another, starting after the SIGNAL symbol describing the
    // RATE and LENGTH fields.
}

const ITransmissionSymbolModel *Ieee80211OFDMModulator::modulate(const ITransmissionBitModel *bitModel) const
{
    std::vector<ISymbol> *ofdmSymbols = new std::vector<ISymbol>(); // FIXME: Sample model should delete it
    const BitVector& bits = bitModel->getBits();
    BitVector signalField;
    for (unsigned int i = 0; i < OFDM_SYMBOL_SIZE; i++)
        signalField.appendBit(bits.getBit(i));
    modulateSignalField(signalField, ofdmSymbols);
    BitVector dataField;
    for (unsigned int i = OFDM_SYMBOL_SIZE; i < bits.getSize(); i++)
        dataField.appendBit(bits.getBit(i));
    modulateDataField(dataField, ofdmSymbols);
    return new TransmissionSymbolModel(0, 0, ofdmSymbols, modulationScheme);
}


} // namespace physicallayer
} // namespace inet
