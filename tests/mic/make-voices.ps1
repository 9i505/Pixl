<#
  The speech for the mic chain tests: sentences made by Windows' own voices at 48 kHz, each saved with the moment every
  word starts (word.tsv beside it), so the tests can measure what happens to the start and the end of every word.
  Runs in PowerShell 7 (its System.Speech also sees Windows' newer voices: Mark, and Naayf for Arabic).
    pwsh -File tests\mic\make-voices.ps1 [-Out build\mic\voices]
#>
param([string]$Out = (Join-Path $PSScriptRoot '..\..\build\mic\voices'))
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $Out | Out-Null
Add-Type -ReferencedAssemblies System.Speech -TypeDefinition @"
using System;
using System.Text;
using System.Speech.Synthesis;
using System.Speech.AudioFormat;
public static class Tts {
    public static string Make(string voice, string text, string wav, int rate) {
        using (var s = new SpeechSynthesizer()) {
            s.SelectVoice(voice);
            s.Rate = rate;
            s.Volume = 100;
            var words = new StringBuilder();
            // AudioPosition counts as if the audio were 16 kHz (the voices' own rate), so at 48 kHz it reads 3 times too late.
            s.SpeakProgress += (o, e) => words.AppendLine(((int)Math.Round(e.AudioPosition.TotalMilliseconds / 3.0)).ToString() + "\t" + e.Text);
            s.SetOutputToWaveFile(wav, new SpeechAudioFormatInfo(48000, AudioBitsPerSample.Sixteen, AudioChannel.Mono));
            s.Speak(text);
            s.SetOutputToNull();
            return words.ToString();
        }
    }
}
"@

# What each sentence is for (the test cases' needs): weak first sounds after silence (h, s, f, th), words ending in quiet
# consonants (s, t, th, f, sh), fast speech with no pauses, a whisperable line, a loud line, and Arabic.
$lines = [ordered]@{
    'onset'   = 'Hey. So, how is it going? Fine, thanks. Thank you for helping. She says hi.'
    'endings' = 'She kept both cats. He laughs at the fifth test. It costs half. Months of breath. Push the rest.'
    'whisper' = 'This is a secret, so please keep it quiet. Just listen for a second.'
    'typing'  = 'I am just typing a quick message while we talk about the plan for tonight.'
    'sudden'  = 'Wait, what was that sound? I think something fell off the shelf.'
    'soft'    = 'So quietly I start the sentence,'
    'loud'    = 'and then I speak up loud and clear.'
    'rapid'   = 'this is just what I said it was so let us get it done fast and then we can go'
    'main'    = 'Can you hear me clearly over there? I want to check the sound before we start.'
    'other'   = 'Yeah I think we should order pizza tonight, what do you think about that idea anyway?'
}
$voices = @{ 'david' = 'Microsoft David Desktop'; 'zira' = 'Microsoft Zira Desktop'; 'mark' = 'Microsoft Mark' }
foreach ($v in $voices.Keys) {
    foreach ($k in $lines.Keys) {
        $rate = if ($k -eq 'rapid') { 5 } else { 0 }
        $wav = Join-Path $Out "$v-$k.wav"
        $tsv = [Tts]::Make($voices[$v], $lines[$k], $wav, $rate)
        Set-Content (Join-Path $Out "$v-$k.words.tsv") $tsv -Encoding UTF8
    }
}
$ar = [Tts]::Make('Microsoft Naayf', 'مرحبا، كيف حالك؟ هذا اختبار لعزل الضوضاء، وأريد أن أسمع كل حرف بوضوح.', (Join-Path $Out 'naayf-arabic.wav'), 0)
Set-Content (Join-Path $Out 'naayf-arabic.words.tsv') $ar -Encoding UTF8
Get-ChildItem $Out -Filter *.wav | Measure-Object | ForEach-Object { "voices: $($_.Count) files in $Out" }
