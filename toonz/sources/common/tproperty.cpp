

#include "tproperty.h"
#include "tstream.h"
#include "texception.h"
//#include "tconvert.h"

void TProperty::addListener(Listener *listener)
{
	if (std::find(m_listeners.begin(), m_listeners.end(), listener) == m_listeners.end())
		m_listeners.push_back(listener);
}

void TProperty::removeListener(Listener *listener)
{
	m_listeners.erase(std::remove(m_listeners.begin(), m_listeners.end(), listener), m_listeners.end());
}

void TProperty::notifyListeners() const
{
	std::vector<Listener *>::const_iterator it;
	for (it = m_listeners.begin(); it != m_listeners.end(); ++it)
		(*it)->onPropertyChanged();
}

//=============================================================================

TPropertyGroup::TPropertyGroup()
{
}

TPropertyGroup::~TPropertyGroup()
{
	for (PropertyVector::iterator it = m_properties.begin();
		 it != m_properties.end(); ++it)
		if (it->second)
			delete it->first;
}

void TPropertyGroup::clear()
{
	m_properties.clear();
	m_table.clear();
}

TPropertyGroup *TPropertyGroup::clone() const
{
	TPropertyGroup *g = new TPropertyGroup();
	for (PropertyVector::const_iterator i = m_properties.begin();
		 i != m_properties.end(); ++i)
		g->add(i->first->clone());
	return g;
}

void TPropertyGroup::add(TProperty *p)
{
	string name = p->getName();
	assert(m_table.find(name) == m_table.end());
	m_properties.push_back(std::make_pair(p, true));
	m_table[name] = p;
}

void TPropertyGroup::bind(TProperty &p)
{
	string name = p.getName();
	assert(m_table.find(name) == m_table.end());
	m_properties.push_back(std::make_pair(&p, false));
	m_table[name] = &p;
}

TProperty *TPropertyGroup::getProperty(string name)
{
	PropertyTable::iterator i = m_table.find(name);
	if (i == m_table.end())
		return 0;
	else
		return i->second;
}

template <class Property>
void assign(Property *dst, TProperty *src)
{
	Property *s = dynamic_cast<Property *>(src);
	if (!s)
		throw TProperty::TypeError();
	dst->setValue(s->getValue());
}

class Setter : public TProperty::Visitor
{
	TProperty *m_src;

public:
	Setter(TProperty *src) : m_src(src) {}

	void visit(TDoubleProperty *dst) { assign(dst, m_src); }
	void visit(TIntProperty *dst) { assign(dst, m_src); }
	void visit(TStringProperty *dst) { assign(dst, m_src); }
	void visit(TBoolProperty *dst) { assign(dst, m_src); }
	void visit(TEnumProperty *dst) { assign(dst, m_src); }
	void visit(TDoublePairProperty *dst) { assign(dst, m_src); }
	void visit(TIntPairProperty *dst) { assign(dst, m_src); }
	void visit(TStyleIndexProperty *dst) { assign(dst, m_src); }
	void visit(TPointerProperty *dst) { assign(dst, m_src); }
};

void TPropertyGroup::setProperties(TPropertyGroup *g)
{
	for (PropertyVector::const_iterator i = g->m_properties.begin();
		 i != g->m_properties.end(); ++i) {
		TProperty *src = i->first;
		TProperty *dst = getProperty(src->getName());
		if (dst) {
			Setter setter(src);
			TProperty::Visitor *visitor = &setter;
			dst->accept(*visitor);
		}
	}
}

void TPropertyGroup::accept(TProperty::Visitor &v)
{
	for (PropertyVector::const_iterator i = m_properties.begin();
		 i != m_properties.end(); ++i)
		i->first->accept(v);
}

class PropertyWriter : public TProperty::Visitor
{
	TOStream &m_os;

public:
	PropertyWriter(TOStream &os) : m_os(os) {}

	void visit(TDoubleProperty *p)
	{
		std::map<string, string> attr;
		attr["type"] = "double";
		attr["name"] = p->getName();
		attr["min"] = toString(p->getRange().first);
		attr["max"] = toString(p->getRange().second);
		attr["value"] = toString(p->getValue());
		m_os.openCloseChild("property", attr);
	}
	void visit(TDoublePairProperty *p)
	{
		std::map<string, string> attr;
		attr["type"] = "pair";
		attr["name"] = p->getName();
		attr["min"] = toString(p->getRange().first);
		attr["max"] = toString(p->getRange().second);
		TDoublePairProperty::Value value = p->getValue();
		attr["value"] = toString(value.first) + " " + toString(value.second);
		m_os.openCloseChild("property", attr);
	}
	void visit(TIntPairProperty *p)
	{
		std::map<string, string> attr;
		attr["type"] = "pair";
		attr["name"] = p->getName();
		attr["min"] = toString(p->getRange().first);
		attr["max"] = toString(p->getRange().second);
		TIntPairProperty::Value value = p->getValue();
		attr["value"] = toString(value.first) + " " + toString(value.second);
		m_os.openCloseChild("property", attr);
	}
	void visit(TIntProperty *p)
	{
		std::map<string, string> attr;
		attr["type"] = "int";
		attr["name"] = p->getName();
		attr["min"] = toString(p->getRange().first);
		attr["max"] = toString(p->getRange().second);
		attr["value"] = toString(p->getValue());
		m_os.openCloseChild("property", attr);
	}
	void visit(TBoolProperty *p)
	{
		std::map<string, string> attr;
		attr["type"] = "bool";
		attr["name"] = p->getName();
		attr["value"] = p->getValue() ? "true" : "false";
		m_os.openCloseChild("property", attr);
	}
	void visit(TStringProperty *p)
	{
		std::map<string, string> attr;
		attr["type"] = "string";
		attr["name"] = p->getName();
		attr["value"] = toString(p->getValue());
		m_os.openCloseChild("property", attr);
	}

	void visit(TStyleIndexProperty *p)
	{
		std::map<string, string> attr;
		attr["type"] = "string";
		attr["name"] = p->getName();
		attr["value"] = p->getValueAsString();
		m_os.openCloseChild("property", attr);
	}

	void visit(TEnumProperty *p)
	{
		std::map<string, string> attr;
		attr["type"] = "enum";
		attr["name"] = p->getName();
		attr["value"] = toString(p->getValue());
		if (TEnumProperty::isRangeSavingEnabled()) {
			m_os.openChild("property", attr);
			std::vector<wstring> range = p->getRange();
			for (int i = 0; i < (int)range.size(); i++) {
				attr.clear();
				attr["value"] = toString(range[i]);
				m_os.openCloseChild("item", attr);
			}
			m_os.closeChild();
		} else
			m_os.openCloseChild("property", attr);
	}
	void visit(TPointerProperty *p)
	{
		std::map<string, string> attr;
		attr["type"] = "pointer";
		attr["name"] = p->getName();
		attr["value"] = p->getValueAsString();
		m_os.openCloseChild("property", attr);
	}
};

void TPropertyGroup::loadData(TIStream &is)
{
	for (PropertyVector::iterator it = m_properties.begin();
		 it != m_properties.end(); ++it)
		if (it->second)
			delete it->first;
	m_properties.clear();
	m_table.clear();
	string tagName;
	while (is.matchTag(tagName)) {
		if (tagName == "property") {
			string name = is.getTagAttribute("name");
			string type = is.getTagAttribute("type");
			string svalue = is.getTagAttribute("value");
			if (name == "")
				throw TException("missing property name");
			if (type == "")
				throw TException("missing property type");
			if (type != "string" && svalue == "")
				throw TException("missing property value");
			if (type == "double") {
				double min = toDouble(is.getTagAttribute("min"));
				double max = toDouble(is.getTagAttribute("max"));
				add(new TDoubleProperty(name, min, max, toDouble(svalue)));
			}
			if (type == "pair") {
				double min = toDouble(is.getTagAttribute("min"));
				double max = toDouble(is.getTagAttribute("max"));
				TDoublePairProperty::Value v(0, 0);
				int i = svalue.find(' ');
				if (i != (int)string::npos) {
					v.first = toDouble(svalue.substr(0, i));
					v.second = toDouble(svalue.substr(i + 1));
				}
				add(new TDoublePairProperty(name, min, max, v.first, v.second));
			} else if (type == "int") {
				int min = toInt(is.getTagAttribute("min"));
				int max = toInt(is.getTagAttribute("max"));
				add(new TIntProperty(name, min, max, toInt(svalue)));
			} else if (type == "bool") {
				if (svalue != "true" && svalue != "false")
					throw TException("bad boolean property value");
				add(new TBoolProperty(name, svalue == "true" ? true : false));
			} else if (type == "string") {
				add(new TStringProperty(name, toWideString(svalue)));
			} else if (type == "enum") {
				TEnumProperty *p = new TEnumProperty(name);
				if (is.isBeginEndTag())
					p->addValue(toWideString(svalue));
				else {
					while (is.matchTag(tagName)) {
						if (tagName == "item") {
							string item = is.getTagAttribute("value");
							p->addValue(toWideString(item));
						} else
							throw TException("expected range property <item>");
					}
					is.closeChild();
				}
				p->setValue(toWideString(svalue));
				add(p);
			} else
				throw TException("unrecognized property type : " + type);
		} else
			throw TException("expected <property>");
		// is.closeChild();
	}
}

void TPropertyGroup::saveData(TOStream &os) const
{
	PropertyWriter writer(os);
	const_cast<TPropertyGroup *>(this)->accept(writer);
}

namespace
{
bool EnumRangeSavingEnabled = true;
}

void TEnumProperty::enableRangeSaving(bool on)
{
	EnumRangeSavingEnabled = on;
}

bool TEnumProperty::isRangeSavingEnabled()
{
	return EnumRangeSavingEnabled;
}