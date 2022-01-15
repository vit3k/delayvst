/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
Delayvst2AudioProcessor::Delayvst2AudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
}

Delayvst2AudioProcessor::~Delayvst2AudioProcessor()
{
}

//==============================================================================
const juce::String Delayvst2AudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool Delayvst2AudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool Delayvst2AudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool Delayvst2AudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double Delayvst2AudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int Delayvst2AudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int Delayvst2AudioProcessor::getCurrentProgram()
{
    return 0;
}

void Delayvst2AudioProcessor::setCurrentProgram (int index)
{
}

const juce::String Delayvst2AudioProcessor::getProgramName (int index)
{
    return {};
}

void Delayvst2AudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void Delayvst2AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    delayLine = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>(sampleRate * 2.f);
    auto spec = juce::dsp::ProcessSpec{
        sampleRate,
        static_cast<juce::uint32>(samplesPerBlock),
        2
    };
    delayLine.prepare(spec);
    previousDelayTime = apvts.getRawParameterValue("Delay time")->load();
    smoothedValue = juce::SmoothedValue<float>(previousDelayTime);
    smoothedValue.reset(sampleRate, .5f);

    lfo.prepare(spec);
    
    lfo.initialise([] (float x) {
        return std::sin(x); }, 128);
    
    lowPass.prepare(spec);

    lowPass.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, apvts.getRawParameterValue("Low pass")->load());
    
    highPass.prepare(spec);
    
    highPass.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 150.f);
    
    allPass.prepare(spec);
    
    allPass.coefficients = juce::dsp::IIR::Coefficients<float>::makeAllPass(sampleRate, 1000.f);
    
}

void Delayvst2AudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool Delayvst2AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void Delayvst2AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);

    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    auto mix = apvts.getRawParameterValue("Mix")->load();
    auto feedback = apvts.getRawParameterValue("Feedback")->load();
    auto delayTime = apvts.getRawParameterValue("Delay time")->load();
    auto modRate = apvts.getRawParameterValue("Mod rate")->load();
    auto modDepth = apvts.getRawParameterValue("Mod depth")->load();
    auto lowPassFreq = apvts.getRawParameterValue("Low pass")->load();
    
    smoothedValue.setTargetValue(delayTime);
    lfo.setFrequency(modRate);
    
    lowPass.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(getSampleRate(), lowPassFreq);
    auto dryMix = 1 - juce::jmax(mix - .5f, 0.f) * 2;
    auto wetMix = mix;
    
    auto* leftData = buffer.getWritePointer (0);
    auto* rightData = buffer.getWritePointer (1);
    
    for (auto i = 0; i < buffer.getNumSamples(); i++) {
        auto lfoSample = lfo.processSample(.0f);
        
        auto newDelayTime = smoothedValue.getNextValue() + lfoSample * (modDepth * 0.001f);
        delayLine.setDelay(getSampleRate() * newDelayTime);

        auto leftDelaySample = delayLine.popSample(0);
        auto rightDelaySample = delayLine.popSample(1);
        
        auto newLeftDelaySample = highPass.processSample(lowPass.processSample(leftData[i] + leftDelaySample * feedback));
        auto newRightDelaySample = highPass.processSample(lowPass.processSample(rightData[i] + rightDelaySample * feedback));
        
        delayLine.pushSample(0, newLeftDelaySample);
        delayLine.pushSample(1, newRightDelaySample);
        
        leftData[i] = leftData[i] * dryMix + leftDelaySample * wetMix;
        rightData[i] =  rightData[i] * dryMix + rightDelaySample * wetMix;
    }
}

//==============================================================================
bool Delayvst2AudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* Delayvst2AudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor(*this);
}

//==============================================================================
void Delayvst2AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
    juce::MemoryOutputStream mos(destData, true);
    apvts.state.writeToStream(mos);
}

void Delayvst2AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    auto tree = juce::ValueTree::readFromData(data, sizeInBytes);
    if( tree.isValid() )
    {
        apvts.replaceState(tree);
    }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Delayvst2AudioProcessor();
}

juce::AudioProcessorValueTreeState::ParameterLayout Delayvst2AudioProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>("Delay time", "Delay time",
                                                           juce::NormalisableRange(.1f, 2.f,
                                                                                   .1f, 1.f),
                                                           .5f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Mix", "Mix",
                                                           juce::NormalisableRange(.0f, 1.f,
                                                                                   .01f, 1.f),
                                                                                   .5f));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>("Feedback", "Feedback",
                                                           juce::NormalisableRange(.0f, 1.f,
                                                                                   .01f, 1.f),
                                                                                   .5f));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>("Mod rate", "Mod rate",
                                                           juce::NormalisableRange(.0f, 5.f,
                                                                                   .1f, 1.f),
                                                                                   1.f));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>("Mod depth", "Mod depth",
                                                           juce::NormalisableRange(.0f, 5.f,
                                                                                   .1f, 1.f),
                                                                                   .01f));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>("Low pass", "Low pass",
                                                           juce::NormalisableRange(1.f, 20000.f,
                                                                                   1.f, 1.f),
                                                                                   .01f));
    return layout;
}
