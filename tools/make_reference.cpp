// SPDX-License-Identifier: GPL-3.0-or-later
// Fabrique les deux fichiers audio de référence pour l'intégration (guide et double synthétiques, vérité connue) :
//   make_reference dossier   ->  guide.wav, double.wav (mono 44,1 kHz, 16 bits) et onsets.txt (temps du guide, temps du double, en secondes)
#include <cstdio>
#include <string>

#define DR_WAV_IMPLEMENTATION
#include "dr_wav/dr_wav.h"

#include "../tests/SynthVoice.h"

static bool writeWav16 (const std::string& path, const std::vector<float>& x)
{
    drwav_data_format f;
    f.container = drwav_container_riff; f.format = DR_WAVE_FORMAT_PCM; f.channels = 1; f.sampleRate = 44100; f.bitsPerSample = 16;
    drwav w;
    if (! drwav_init_file_write (&w, path.c_str(), &f, nullptr)) return false;
    std::vector<drwav_int16> pcm (x.size());
    for (size_t i = 0; i < x.size(); ++i) pcm[i] = (drwav_int16) std::lround (std::max (-1.0f, std::min (1.0f, x[i])) * 32767.0f);
    drwav_write_pcm_frames (&w, pcm.size(), pcm.data());
    drwav_uninit (&w);
    return true;
}

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    const auto p = synth::makePair (21, 0.10, 0, 11);
    writeWav16 (dir + "/guide.wav", p.guide.audio);
    writeWav16 (dir + "/double.wav", p.dub.audio);
    FILE* f = std::fopen ((dir + "/onsets.txt").c_str(), "w");
    std::fprintf (f, "# temps du guide (s), temps du double (s) : début de chaque note, vérité connue de la voix synthétique\n");
    for (size_t k = 0; k < p.truth.points.size(); k += 2) std::fprintf (f, "%.4f %.4f\n", p.truth.points[k].first, p.truth.points[k].second);
    std::fclose (f);
    std::printf ("guide %.2f s, double %.2f s, %zu notes\n", p.guide.audio.size() / 44100.0, p.dub.audio.size() / 44100.0, p.truth.points.size() / 2);
    return 0;
}
