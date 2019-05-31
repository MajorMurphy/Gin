/*
  ==============================================================================

    This file was auto-generated by the Introjucer!

    It contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "program.h"
#include "processor.h"

//==============================================================================
GinProcessor::GinProcessor()
{
    LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        
    properties = std::make_unique<PropertiesFile> (getSettingsFile(), PropertiesFile::Options());
    
    loadAllPrograms();
    
    state = ValueTree (Identifier ("state"));
    
    stateUpdated();
}

GinProcessor::~GinProcessor()
{    
    MessageManagerLock mmLock;
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

std::unique_ptr<PropertiesFile> GinProcessor::getSettings()
{
#if JUCE_MAC
    File dir = File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Preferences").getChildFile ("SocaLabs");
#else
    File dir = File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("SocaLabs");
#endif
    dir.createDirectory();
    
    PropertiesFile::Options options;
    
    return std::make_unique<PropertiesFile> (dir.getChildFile ("plugin_settings.xml"), options);
}

//==============================================================================
void GinProcessor::addPluginParameter (Parameter* parameter)
{
    addParameter (parameter);
    
    parameterMap[parameter->getUid()] = parameter;
}

Parameter* GinProcessor::getParameter (const String& uid)
{
    if (parameterMap.find (uid) != parameterMap.end())
        return parameterMap[uid];
    
    return nullptr;
}

float GinProcessor::parameterValue (const String& uid)
{
    if (parameterMap.find (uid) != parameterMap.end())
        return parameterMap[uid]->getUserValue();
    
    return 0;
    
}

int GinProcessor::parameterIntValue (const String& uid)
{
    if (parameterMap.find (uid) != parameterMap.end())
        return int (parameterMap[uid]->getUserValue());
    
    return 0;
}

bool GinProcessor::parameterBoolValue (const String& uid)
{
    if (parameterMap.find (uid) != parameterMap.end())
        return parameterMap[uid]->getUserValue() > 0;
    
    return 0;
}

Array<Parameter*> GinProcessor::getPluginParameters()
{
    Array<Parameter*> result;
    
    auto params = getParameters();
    for (auto p : params)
        if (auto pp = dynamic_cast<Parameter*>(p))
            result.add (pp);
    
    return result;
}

//==============================================================================
const String GinProcessor::getName() const
{
   #ifdef JucePlugin_Name
    return JucePlugin_Name;
   #else
    return {};
   #endif
}

bool GinProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool GinProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

double GinProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int GinProcessor::getNumPrograms()
{
    return programs.size();
}

int GinProcessor::getCurrentProgram()
{
    return currentProgram;
}

void GinProcessor::setCurrentProgram (int index)
{
    if (index == getCurrentProgram())
        return;
    if ((Time::getCurrentTime() - lastStateLoad) < RelativeTime::seconds (2))
        return;

    if (index >= 0 && index < programs.size())
    {
        programs[index]->loadProcessor (this);
        currentProgram = index;
        
        updateHostDisplay();
        sendChangeMessage();
        stateUpdated();
    }
}

const String GinProcessor::getProgramName (int index)
{
    return programs[index]->name;
}

void GinProcessor::changeProgramName (int index, const String& newName)
{
    programs[index]->deleteFromDir (getProgramDirectory());
    programs[index]->name = newName;
    programs[index]->saveToDir (getProgramDirectory());
    
    updateHostDisplay();
    sendChangeMessage();
}

void GinProcessor::loadAllPrograms()
{
    programs.clear();
    
    // create the default program
    GinProgram* defaultProgram = new GinProgram();
    defaultProgram->name = "Default";
    defaultProgram->saveProcessor (this);
    
    programs.add (defaultProgram);

    // load programs from disk
    File dir = getProgramDirectory();
    
    Array<File> programFiles;
    dir.findChildFiles (programFiles, File::findFiles, false, "*.xml");
    
    for (File f : programFiles)
    {
        GinProgram* program = new GinProgram();
        program->loadFromFile (f);
        programs.add (program);
    }
}

void GinProcessor::saveProgram (String name)
{
    updateState();
    
    for (int i = programs.size(); --i >= 0;)
        if (programs[i]->name == name)
            deleteProgram (i);

    GinProgram* newProgram = new GinProgram();
    newProgram->name = name;
    newProgram->saveProcessor (this);
    newProgram->saveToDir (getProgramDirectory());
    
    programs.add (newProgram);
    currentProgram = programs.size() - 1;
    
    updateHostDisplay();
    sendChangeMessage();
}

void GinProcessor::deleteProgram (int index)
{
    programs[index]->deleteFromDir (getProgramDirectory());
    programs.remove (index);
    if (index <= currentProgram)
        currentProgram--;
    
    updateHostDisplay();
    sendChangeMessage();
}

File GinProcessor::getProgramDirectory()
{
  #ifdef JucePlugin_Name
   #if JUCE_MAC
    File dir = File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("Application Support/com.socalabs/" JucePlugin_Name "/programs");
   #else
    File dir = File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("com.socalabs/" JucePlugin_Name "/programs");
   #endif
  #else
    // Shouldn't be using processor in something that isn't a plugin
    jassertfalse;
    File dir;
  #endif
    
    if (!dir.isDirectory())
        dir.createDirectory();
    return dir;
}

File GinProcessor::getSettingsFile()
{
  #ifdef JucePlugin_Name
   #if JUCE_MAC
    File file = File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("Application Support/com.socalabs/" JucePlugin_Name "/settings.xml");
   #else
    File file = File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("com.socalabs/" JucePlugin_Name "/settings.xml");
   #endif
  #else
    // Shouldn't be using processor in something that isn't a plugin
    jassertfalse;
    File file;
  #endif

    if (!file.getParentDirectory().isDirectory())
        file.getParentDirectory().createDirectory();
    return file;
}
//==============================================================================

void GinProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    updateState();
    
    std::unique_ptr<XmlElement> rootE (new XmlElement ("state"));
    
    if (state.isValid())
        rootE->setAttribute ("valueTree", state.toXmlString());
    
    rootE->setAttribute("program", currentProgram);
    
    for (Parameter* p : getPluginParameters())
    {
        if (! p->isMetaParameter())
        {
            Parameter::ParamState pstate = p->getState();
            
            XmlElement* paramE = new XmlElement ("param");
            
            paramE->setAttribute ("uid", pstate.uid);
            paramE->setAttribute ("val", pstate.value);
            
            rootE->addChildElement (paramE);
        }
    }
    
    MemoryOutputStream os (destData, true);
    auto text = rootE->toString();
    os.write (text.toRawUTF8(), text.getNumBytesAsUTF8());
}

void GinProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    XmlDocument doc (String::fromUTF8 ((const char*)data, sizeInBytes));
    std::unique_ptr<XmlElement> rootE (doc.getDocumentElement());
    if (rootE)
    {
        if (rootE->hasAttribute ("valueTree"))
        {
            String xml = rootE->getStringAttribute ("valueTree");
            XmlDocument treeDoc (xml);
            if (std::unique_ptr<XmlElement> vtE = treeDoc.getDocumentElement())
                state = ValueTree::fromXml (*vtE.get());
        }
        
        currentProgram = rootE->getIntAttribute ("program");
        
        XmlElement* paramE = rootE->getChildByName ("param");
        while (paramE)
        {
            String uid = paramE->getStringAttribute ("uid");
            float  val = paramE->getStringAttribute ("val").getFloatValue();
            
            if (Parameter* p = getParameter (uid))
            {
                if (! p->isMetaParameter())
                    p->setUserValue (val);
            }
            
            paramE = paramE->getNextElementWithTagName ("param");
        }
    }
    stateUpdated();

    lastStateLoad = Time::getCurrentTime();
}

