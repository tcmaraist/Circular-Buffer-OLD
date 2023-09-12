/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
CircularBufferAudioProcessor::CircularBufferAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ), treeState(*this, nullptr, "PARAMETERS", createParameterLayout() )
#endif
{
}

CircularBufferAudioProcessor::~CircularBufferAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout CircularBufferAudioProcessor::createParameterLayout()
{
    std::vector <std::unique_ptr<juce::RangedAudioParameter>>params;
    auto pGain = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID("gain", 1), "Gain", -24.0, 24.0, 0.0);
    params.push_back(std::move(pGain));
    return { params.begin(), params.end()};
}

//==============================================================================
const juce::String CircularBufferAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool CircularBufferAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool CircularBufferAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool CircularBufferAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double CircularBufferAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int CircularBufferAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int CircularBufferAudioProcessor::getCurrentProgram()
{
    return 0;
}

void CircularBufferAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String CircularBufferAudioProcessor::getProgramName (int index)
{
    return {};
}

void CircularBufferAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void CircularBufferAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numInputChannels = getTotalNumInputChannels();
    const int delayBufferSize = 2 * (sampleRate + samplesPerBlock);
    mSampleRate = sampleRate;
    mDelayBuffer.setSize(numInputChannels, delayBufferSize);
    
}

void CircularBufferAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool CircularBufferAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

void CircularBufferAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    
    
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());
    
    const int bufferLength = buffer.getNumSamples();
    const int delayBufferLength = mDelayBuffer.getNumSamples();
  
    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        const float* bufferData = buffer.getReadPointer(channel);
        const float* delayBufferData = mDelayBuffer.getReadPointer(channel);
        float* dryBuffer = buffer.getWritePointer(channel);
        
        fillDelayBuffer(channel, bufferLength, delayBufferLength, bufferData, delayBufferData);
        getFromDelayBuffer(buffer, channel, bufferLength, delayBufferLength, bufferData, delayBufferData);
        feedbackDelay(channel, bufferLength, delayBufferLength, dryBuffer);
    }
    
    mWritePosition += bufferLength;
    mWritePosition %= delayBufferLength;
}

void CircularBufferAudioProcessor::fillDelayBuffer(int channel, const int bufferLength, const int delayBufferLength, const float* bufferData, const float* delayBufferData)
{
    //float dbGain = *treeState.getRawParameterValue("gain");
    //float rawGain = juce::Decibels::decibelsToGain(dbGain);
    //float gain = rawGain;
    const float level = 0.3;
    
        if (delayBufferLength > bufferLength + mWritePosition)
        {
            mDelayBuffer.copyFromWithRamp(channel, mWritePosition, bufferData, bufferLength, level, level);
        }
        else {
            const int bufferRemaining = delayBufferLength - mWritePosition;
            
            mDelayBuffer.copyFromWithRamp(channel, mWritePosition, bufferData, bufferRemaining, level, level);
            mDelayBuffer.copyFromWithRamp(channel, 0, bufferData, bufferLength - bufferRemaining, level, level);
        }
}

void CircularBufferAudioProcessor::getFromDelayBuffer(juce::AudioBuffer<float>& buffer, int channel, const int bufferLength, const int delayBufferLength, const float* bufferData, const float* delayBufferData)
{

    
    int delayTime = 200;
    const int readPosition = static_cast<int>( delayBufferLength + mWritePosition - ( mSampleRate * delayTime / 1000 )) % delayBufferLength;
    
    if (delayBufferLength > bufferLength + readPosition)
    {
        buffer.copyFrom(channel, 0, delayBufferData + readPosition, bufferLength);
    }
    else {
        const int bufferRemaining = delayBufferLength - readPosition;
        buffer.copyFrom(channel, 0, delayBufferData + readPosition, bufferRemaining);
        buffer.copyFrom(channel, bufferRemaining, delayBufferData, bufferLength - bufferRemaining);
    }
    
}

void CircularBufferAudioProcessor::feedbackDelay(int channel, const int bufferLength, const int delayBufferLength, float* dryBuffer)
{
    if (delayBufferLength > bufferLength + mWritePosition)
    {
        mDelayBuffer.addFromWithRamp(channel, mWritePosition, dryBuffer, bufferLength, 0.8, 0.8);
    }
    else {
        const int bufferRemaining = delayBufferLength - mWritePosition;
        
        mDelayBuffer.addFromWithRamp(channel, bufferRemaining, dryBuffer, bufferRemaining, 0.8, 0.8);
        mDelayBuffer.addFromWithRamp(channel, 0, dryBuffer, bufferLength - bufferRemaining, 0.8, 0.8);
    }
}
//==============================================================================
bool CircularBufferAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* CircularBufferAudioProcessor::createEditor()
{
   // return new CircularBufferAudioProcessorEditor (*this);
    return new juce::GenericAudioProcessorEditor (*this);
}

//==============================================================================
void CircularBufferAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void CircularBufferAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CircularBufferAudioProcessor();
}
