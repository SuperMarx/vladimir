#pragma once

#include <functional>
#include <stdexcept>
#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>

#include <supermarx/scraper/util.hpp>
#include <supermarx/scraper/html_parser.hpp>
#include <supermarx/scraper/html_watcher.hpp>
#include <supermarx/scraper/html_recorder.hpp>

#include <vladimir/scraper.hpp>

namespace supermarx
{
class category_parser : public html_parser::default_handler
{
private:
	enum state_e {
		S_INIT,
		S_CATEGORY,
		S_BLOCKED
	};

public:
	typedef std::function<void(scraper::category)> category_callback_t;
	typedef std::function<void(size_t)> id_callback_t;

private:
	category_callback_t category_callback;
	id_callback_t id_callback;

	boost::optional<html_recorder> rec;
	html_watcher_collection wc;

	state_e state;

	boost::optional<scraper::category> candidate;

public:
	category_parser(category_callback_t category_callback_, id_callback_t id_callback_)
		: category_callback(category_callback_)
		, id_callback(id_callback_)
		, rec()
		, wc()
		, state(S_INIT)
		, candidate()
	{}

	template<typename T>
	void parse(T source)
	{
		html_parser::parse(source, *this);
	}

	virtual void startElement(const std::string& /* namespaceURI */, const std::string& /* localName */, const std::string& qName, const AttributesT& atts)
	{
		if(rec) {
			rec.get().startElement();
		}

		wc.startElement();

		switch(state)
		{
		case S_INIT:
			if(atts.getValue("id") == "header-mainnav") {
				state = S_BLOCKED;
				wc.add([&]() {
					state = S_INIT;
				});
			} else if(qName == "li" && util::contains_attr("categoryItem", atts.getValue("class"))) {
				state = S_CATEGORY;
				candidate = scraper::category{"", ""};

				wc.add([&]() {
					state = S_INIT;

					if (candidate) {
						category_callback(*candidate);
						candidate = boost::none;
					}
				});
			} else if (qName == "input" && atts.getValue("name") == "CategoryName") {
				id_callback(boost::lexical_cast<size_t>(atts.getValue("value")));
			}
			break;
		case S_CATEGORY:
			if(qName == "a") {
				candidate->url = atts.getValue("href");
			} else if(util::contains_attr("title", atts.getValue("class"))) {
				rec = html_recorder(
							[&](std::string ch) { candidate->name = util::sanitize(ch); }
						);
			}
			break;
		case S_BLOCKED:
			// Do nothing
			break;
		}
	}

	virtual void characters(const std::string& ch)
	{
		if(rec) {
			rec->characters(ch);
		}
	}

	virtual void endElement(const std::string& /* namespaceURI */, const std::string& /* localName */, const std::string& /* qName */)
	{
		if(rec && rec.get().endElement()) {
			rec = boost::none;
		}

		wc.endElement();
	}
};
}
