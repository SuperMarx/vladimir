#include "scraper.hpp"

namespace supermarx
{
	scraper::scraper(callback_t _callback, unsigned int _ratelimit)
	: callback(_callback)
	, dl("supermarx vladimir/1.0", _ratelimit)
	{}

	void scraper::scrape()
	{
		// Stub
	}
}
