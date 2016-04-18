

#include "tenv.h"
#include "tsystem.h"
#include "tconvert.h"
#include "tfilepath_io.h"

#include <QSettings>

#ifdef LEVO_MACOSX

#include "macofflinegl.h"
#include "tofflinegl.h"

// Imposto l'offlineGL usando AGL (per togliere la dipendenza da X)

TOfflineGL::Imp *MacOfflineGenerator1(const TDimension &dim)
{
	return new MacImplementation(dim);
}
#endif

//#include <typeinfo>

//#include <ctype.h>
//#include <stdlib.h>

//using namespace std;

#include <map>
//#include <fstream.h>
//#include <strstream.h>

using namespace TEnv;

//=========================================================
//
// root dir
//
//=========================================================

namespace
{

class EnvGlobals
{ // singleton

	string m_applicationName;
	string m_applicationVersion;
	string m_applicationFullName;
	string m_moduleName;
	string m_rootVarName;
	string m_systemVarPrefix;
	TFilePath m_registryRoot;
	TFilePath m_envFile;
	TFilePath *m_stuffDir;
	TFilePath *m_dllRelativeDir;

	EnvGlobals() : m_stuffDir(0) {}

public:
	~EnvGlobals() { delete m_stuffDir; }

	static EnvGlobals *instance()
	{
		static EnvGlobals _instance;
		return &_instance;
	}

	TFilePath getSystemVarPath(string varName)
	{
#ifdef WIN32
		return m_registryRoot + varName;
#else
		QString settingsPath = QString::fromStdString(getApplicationName()) + QString("_") +
							   QString::fromStdString(getApplicationVersion()) + QString(".app") +
							   QString("/Contents/Resources/SystemVar.ini");
		QSettings settings(settingsPath, QSettings::IniFormat);
		QString qStr = QString::fromStdString(varName);
		QString systemVar = settings.value(qStr).toString();
		//printf("getSystemVarPath: path:%s key:%s var:%s\n", settingsPath.toStdString().data(), varName.data(), systemVar.toStdString().data());
		return TFilePath(systemVar.toStdWString());
#endif
	}

	TFilePath getRootVarPath()
	{
		return getSystemVarPath(m_rootVarName);
	}

	string getSystemVarValue(string varName)
	{
#ifdef WIN32
		return TSystem::getSystemValue(getSystemVarPath(varName)).toStdString();
#else
		TFilePath systemVarPath = getSystemVarPath(varName);
		if (systemVarPath.isEmpty()) {
			std::cout << "varName:" << varName << " TOONZROOT not set..." << std::endl;
			return "";
		}
		return toString(systemVarPath);
/*
			char *value = getenv(varName.c_str());
			if (!value)
				{
				std::cout << varName << " not set, returning TOONZROOT" << std::endl;
        //value = getenv("TOONZROOT");
                        value="";
                        std::cout << "!!!value= "<< value << std::endl;
   			if (!value)
					{
 				        std::cout << varName << "TOONZROOT not set..." << std::endl;
					//exit(-1);
					return "";
					}
				}
      return string(value);
	*/
#endif
	}

	TFilePath getStuffDir()
	{
		if (m_stuffDir)
			return *m_stuffDir;
		return TFilePath(getSystemVarValue(m_rootVarName));
	}
	void setStuffDir(const TFilePath &stuffDir)
	{
		delete m_stuffDir;
		m_stuffDir = new TFilePath(stuffDir);
	}

	void updateEnvFile()
	{
		TFilePath profilesDir = getSystemVarPathValue(getSystemVarPrefix() + "PROFILES");
		if (profilesDir == TFilePath())
			profilesDir = getStuffDir() + "profiles";
		m_envFile = profilesDir + "env" + (TSystem::getUserName().toStdString() + ".env");
	}

	void setApplication(string applicationName, string applicationVersion)
	{
		m_applicationName = applicationName;
		m_applicationVersion = applicationVersion;
		m_applicationFullName = m_applicationName + " " + m_applicationVersion;
		m_moduleName = m_applicationName;
		m_rootVarName = toUpper(m_applicationName) + "ROOT";
#ifdef WIN32
		m_registryRoot = TFilePath("SOFTWARE\\OpenToonz\\") + m_applicationName + m_applicationVersion;
#endif
		m_systemVarPrefix = m_applicationName;
		updateEnvFile();
	}

	string getApplicationName() { return m_applicationName; }
	string getApplicationVersion() { return m_applicationVersion; }

	TFilePath getEnvFile() { return m_envFile; }

	void setApplicationFullName(string applicationFullName)
	{
		m_applicationFullName = applicationFullName;
	}
	string getApplicationFullName() { return m_applicationFullName; }

	void setModuleName(string moduleName) { m_moduleName = moduleName; }
	string getModuleName() { return m_moduleName; }

	void setRootVarName(string varName)
	{
		m_rootVarName = varName;
		updateEnvFile();
	}
	string getRootVarName()
	{
		return m_rootVarName;
	}

	void setSystemVarPrefix(string prefix)
	{
		m_systemVarPrefix = prefix;
	}
	string getSystemVarPrefix() { return m_systemVarPrefix; }

	void setDllRelativeDir(const TFilePath &dllRelativeDir)
	{
		delete m_dllRelativeDir;
		m_dllRelativeDir = new TFilePath(dllRelativeDir);
	}

	TFilePath getDllRelativeDir()
	{
		if (m_dllRelativeDir)
			return *m_dllRelativeDir;
		return TFilePath(".");
	}
};

/*
TFilePath EnvGlobals::getSystemPath(int id)
{
  std::map<int, TFilePath>::iterator it = m_systemPaths.find(id);
  if(it != m_systemPaths.end()) return it->second;
  switch(id)
    {
     case StuffDir:        return TFilePath(); 
     case ConfigDir:       return getSystemPath(StuffDir) + "config";
     case ProfilesDir:      return getSystemPath(StuffDir) + "profiles";
     default: return TFilePath();      
    }
}

void EnvGlobals::setSystemPath(int id, const TFilePath &fp)
{
  m_systemPaths[id] = fp;
}
*/

} // namespace

//=========================================================
//
// Variable::Imp
//
//=========================================================

class Variable::Imp
{
public:
	string m_name;
	string m_value;
	bool m_loaded, m_defaultDefined, m_assigned;

	Imp(string name)
		: m_name(name), m_value(""), m_loaded(false), m_defaultDefined(false), m_assigned(false) {}
};

//=========================================================
//
// varaible manager (singleton)
//
//=========================================================

namespace
{

class VariableSet
{

	std::map<string, Variable::Imp *> m_variables;
	bool m_loaded;

public:
	VariableSet() : m_loaded(false) {}

	~VariableSet()
	{
		std::map<string, Variable::Imp *>::iterator it;
		for (it = m_variables.begin(); it != m_variables.end(); ++it)
			delete it->second;
	}

	static VariableSet *instance()
	{
		static VariableSet instance;
		return &instance;
	}

	Variable::Imp *getImp(string name)
	{
		std::map<string, Variable::Imp *>::iterator it;
		it = m_variables.find(name);
		if (it == m_variables.end()) {
			Variable::Imp *imp = new Variable::Imp(name);
			m_variables[name] = imp;
			return imp;
		} else
			return it->second;
	}

	void commit()
	{
		//save();
	}

	void loadIfNeeded()
	{
		if (m_loaded)
			return;
		m_loaded = true;
		try {
			load();
		} catch (...) {
		}
	}

	void load();
	void save();
};

//-------------------------------------------------------------------

void VariableSet::load()
{
#ifdef MACOSX
	EnvGlobals::instance()->updateEnvFile();
#endif
	TFilePath fp = EnvGlobals::instance()->getEnvFile();
	if (fp == TFilePath())
		return;
	Tifstream is(fp);
	if (!is)
		return;
	char buffer[1024];
	while (is.getline(buffer, sizeof(buffer))) {
		char *s = buffer;
		while (*s == ' ')
			s++;
		char *t = s;
		while ('a' <= *s && *s <= 'z' || 'A' <= *s && *s <= 'Z' || '0' <= *s && *s <= '9' || *s == '_')
			s++;
		string name(t, s - t);
		if (name.size() == 0)
			continue;
		while (*s == ' ')
			s++;
		if (*s != '\"')
			continue;
		s++;
		string value;
		while (*s != '\n' && *s != '\0' && *s != '\"') {
			if (*s != '\\')
				value.push_back(*s);
			else {
				s++;
				if (*s == '\\')
					value.push_back('\\');
				else if (*s == '"')
					value.push_back('"');
				else if (*s == 'n')
					value.push_back('\n');
				else
					continue;
			}
			s++;
		}
		Variable::Imp *imp = getImp(name);
		imp->m_value = value;
		imp->m_loaded = true;
	}
}

//-------------------------------------------------------------------

void VariableSet::save()
{
	TFilePath fp = EnvGlobals::instance()->getEnvFile();
	if (fp == TFilePath())
		return;
	bool exists = TFileStatus(fp.getParentDir()).doesExist();
	if (!exists) {
		try {
			TSystem::mkDir(fp.getParentDir());
		} catch (...) {
			return;
		}
	}
	Tofstream os(fp);
	if (!os)
		return;
	std::map<string, Variable::Imp *>::iterator it;
	for (it = m_variables.begin(); it != m_variables.end(); ++it) {
		os << it->first << " \"";
		string s = it->second->m_value;
		for (int i = 0; i < (int)s.size(); i++)
			if (s[i] == '\"')
				os << "\\\"";
			else if (s[i] == '\\')
				os << "\\\\";
			else if (s[i] == '\n')
				os << "\\n";
			else
				os.put(s[i]);
		os << "\"" << std::endl;
	}
}

//-------------------------------------------------------------------

} // namespace

//=========================================================

Variable::Variable(string name)
	: m_imp(VariableSet::instance()->getImp(name))
{
}

//-------------------------------------------------------------------

Variable::Variable(string name, string defaultValue)
	: m_imp(VariableSet::instance()->getImp(name))
{
	//assert(!m_imp->m_defaultDefined);
	m_imp->m_defaultDefined = true;
	if (!m_imp->m_loaded)
		m_imp->m_value = defaultValue;
}

//-------------------------------------------------------------------

Variable::~Variable()
{
}

//-------------------------------------------------------------------

string Variable::getName() const
{
	return m_imp->m_name;
}

//-------------------------------------------------------------------

string Variable::getValue() const
{
	VariableSet::instance()->loadIfNeeded();
	return m_imp->m_value;
}

//-------------------------------------------------------------------

void Variable::assignValue(string value)
{
	VariableSet *vs = VariableSet::instance();
	vs->loadIfNeeded();
	m_imp->m_value = value;
	try {
		vs->commit();
	} catch (...) {
	}
}

//===================================================================

void TEnv::setApplication(string applicationName, string applicationVersion)
{
	EnvGlobals::instance()->setApplication(applicationName, applicationVersion);

#ifdef LEVO_MACOSX
	TOfflineGL::defineImpGenerator(MacOfflineGenerator1);
#endif
}

string TEnv::getApplicationName()
{
	return EnvGlobals::instance()->getApplicationName();
}

string TEnv::getApplicationVersion()
{
	return EnvGlobals::instance()->getApplicationVersion();
}

void TEnv::setApplicationFullName(string applicationFullName)
{
	EnvGlobals::instance()->setApplicationFullName(applicationFullName);
}

string TEnv::getApplicationFullName()
{
	return EnvGlobals::instance()->getApplicationFullName();
}

void TEnv::setModuleName(string moduleName)
{
	EnvGlobals::instance()->setModuleName(moduleName);
}

string TEnv::getModuleName()
{
	return EnvGlobals::instance()->getModuleName();
}

void TEnv::setRootVarName(string varName)
{
	EnvGlobals::instance()->setRootVarName(varName);
}

string TEnv::getRootVarName()
{
	return EnvGlobals::instance()->getRootVarName();
}

TFilePath TEnv::getRootVarPath()
{
	return EnvGlobals::instance()->getRootVarPath();
}

string TEnv::getSystemVarStringValue(string varName)
{
	return EnvGlobals::instance()->getSystemVarValue(varName);
}

TFilePath TEnv::getSystemVarPathValue(string varName)
{
	EnvGlobals *eg = EnvGlobals::instance();
	return TFilePath(eg->getSystemVarValue(varName));
}

TFilePathSet TEnv::getSystemVarPathSetValue(string varName)
{
	TFilePathSet lst;
	string value = EnvGlobals::instance()->getSystemVarValue(varName);
	int len = (int)value.size();
	int i = 0;
	int j = value.find(';');
	while (j != string::npos) {
		string s = value.substr(i, j - i);
		lst.push_back(TFilePath(s));
		i = j + 1;
		if (i >= len)
			return lst;
		j = value.find(';', i);
	}
	if (i < len)
		lst.push_back(TFilePath(value.substr(i)));
	return lst;
}

void TEnv::setSystemVarPrefix(string varName)
{
	EnvGlobals::instance()->setSystemVarPrefix(varName);
}

string TEnv::getSystemVarPrefix()
{
	return EnvGlobals::instance()->getSystemVarPrefix();
}

TFilePath TEnv::getStuffDir()
{
	//#ifdef MACOSX
	//return TFilePath("/Applications/Toonz 5.0/Toonz 5.0 stuff");
	//#else
	return EnvGlobals::instance()->getStuffDir();
	//#endif
}

TFilePath TEnv::getConfigDir()
{
	TFilePath configDir = getSystemVarPathValue(getSystemVarPrefix() + "CONFIG");
	if (configDir == TFilePath())
		configDir = getStuffDir() + "config";
	return configDir;
}

/*TFilePath TEnv::getProfilesDir()
{
  TFilePath fp(getStuffDir());
  return fp != TFilePath() ? fp + "profiles" : fp;
}
*/
void TEnv::setStuffDir(const TFilePath &stuffDir)
{
	EnvGlobals::instance()->setStuffDir(stuffDir);
}

TFilePath TEnv::getDllRelativeDir()
{
	return EnvGlobals::instance()->getDllRelativeDir();
}

void TEnv::setDllRelativeDir(const TFilePath &dllRelativeDir)
{
	EnvGlobals::instance()->setDllRelativeDir(dllRelativeDir);
}

void TEnv::saveAllEnvVariables()
{
	VariableSet::instance()->save();
}

/*
void TEnv::defineSystemPath(SystemFileId id, const TFilePath &registryName)
{
  string s = TSystem::getSystemValue(registryName);
  if(s=="") return;
  EnvGlobals::instance()->setSystemPath(id, TFilePath(s));
}

//---------------------------------------------------------


TFilePath TEnv::getSystemPath(SystemFileId id)
{
  return EnvGlobals::instance()->getSystemPath(id);
}
*/

//=========================================================
//
// Variabili tipizzate
//
//=========================================================

namespace
{

istream &operator>>(istream &is, TFilePath &path)
{
	string s;
	is >> s;
	//path = TFilePath(s);
	return is;
}

istream &operator>>(istream &is, TRect &rect)
{
	return is >> rect.x0 >> rect.y0 >> rect.x1 >> rect.y1;
}

template <class T>
string toString2(T value)
{
	ostringstream ss;
	ss << value << '\0';
	string s(ss.str());
	return s;
}

template <>
string toString2(TRect value)
{
	ostringstream ss;
	ss << value.x0 << " " << value.y0 << " " << value.x1 << " " << value.y1 << '\0';
	string s = ss.str();
	return s;
}

template <class T>
void fromString(string s, T &value)
{
	if (s.empty())
		return;
	istringstream is(s.c_str());
	is >> value;
}

void fromString(string s, string &value)
{
	value = s;
}

} // namespace

//-------------------------------------------------------------------

IntVar::IntVar(string name, int defValue) : Variable(name, toString(defValue)) {}
IntVar::IntVar(string name) : Variable(name) {}
IntVar::operator int() const
{
	int v;
	fromString(getValue(), v);
	return v;
}
void IntVar::operator=(int v) { assignValue(toString(v)); }

//-------------------------------------------------------------------

DoubleVar::DoubleVar(string name, double defValue) : Variable(name, toString(defValue)) {}
DoubleVar::DoubleVar(string name) : Variable(name) {}
DoubleVar::operator double() const
{
	double v;
	fromString(getValue(), v);
	return v;
}
void DoubleVar::operator=(double v) { assignValue(toString(v)); }

//-------------------------------------------------------------------

StringVar::StringVar(string name, const string &defValue) : Variable(name, defValue) {}
StringVar::StringVar(string name) : Variable(name) {}
StringVar::operator string() const
{
	string v;
	fromString(getValue(), v);
	return v;
}
void StringVar::operator=(const string &v) { assignValue(v); }

//-------------------------------------------------------------------

FilePathVar::FilePathVar(string name, const TFilePath &defValue) : Variable(name, toString(defValue)) {}
FilePathVar::FilePathVar(string name) : Variable(name) {}
FilePathVar::operator TFilePath() const
{
	string v;
	fromString(getValue(), v);
	return TFilePath(v);
}
void FilePathVar::operator=(const TFilePath &v) { assignValue(toString(v)); }

//-------------------------------------------------------------------

RectVar::RectVar(string name, const TRect &defValue) : Variable(name, toString2(defValue)) {}
RectVar::RectVar(string name) : Variable(name) {}
RectVar::operator TRect() const
{
	TRect v;
	fromString(getValue(), v);
	return v;
}
void RectVar::operator=(const TRect &v) { assignValue(toString2(v)); }

//=========================================================