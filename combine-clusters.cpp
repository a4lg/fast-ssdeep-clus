/*

	fast-ssdeep-clus : Parallel ssdeep clustering kit

	combine-clusters.cpp
	Cluster combining program

	Copyright (C) 2017 Tsukasa OI <floss_ssdeep@irq.a4lg.com>


	Permission to use, copy, modify, and/or distribute this software for
	any purpose with or without fee is hereby granted, provided that the
	above copyright notice and this permission notice appear in all copies.

	THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
	WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
	MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
	ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
	WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
	ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
	OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

*/
/*
	Note:
	This program skips certain error checks.
	So, this program is not suited to accept arbitrary file.
*/

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <chrono>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace std;


struct pstr_hash_type
{
	size_t operator()(const std::string* str) const
	{
		return std::hash<std::string>()(*str);
	}
};

struct pstr_pred_type
{
	bool operator()(const std::string* str1, const std::string* str2) const
	{
		return *str1 == *str2;
	}
};


int main(int argc, char** argv)
{
	// We are no longer interested to the details of digest string.
	// This program handles digest by simple string.
	bool print_progress = true;
	unsigned long long interval = 1000;
	unsigned long long cluster_count = 0;
	const char* comment = nullptr;
	auto t_start = chrono::system_clock::now();
	// Read arguments
	try
	{
		for (int i = 1; i < argc; i++)
		{
			size_t idx;
			char* arg;
			if (!strcmp(argv[i], "-i"))
			{
				if (++i == argc)
				{
					fprintf(stderr, "error: specify actual interval.\n");
					return 1;
				}
				arg = argv[i];
				interval = stoull(arg, &idx, 10);
				if (arg[idx])
					throw invalid_argument("cannot parse interval value");
				if (interval < 1)
					throw out_of_range("check interval is out of range");
			}
			else if (!strcmp(argv[i], "-c"))
			{
				if (++i == argc)
				{
					fprintf(stderr, "error: specify actual comment.\n");
					return 1;
				}
				comment = argv[i];
			}
			else if (!strcmp(argv[i], "-np"))
			{
				print_progress = false;
			}
			else
			{
				fprintf(stderr, "error: invalid option is given.\n");
				return 1;
			}
		}
		if (!comment)
			comment = "combining";
	}
	catch (invalid_argument)
	{
		fprintf(stderr, "error: invalid argument is given.\n");
		return 1;
	}
	catch (out_of_range)
	{
		fprintf(stderr, "error: out of range argument is given.\n");
		return 1;
	}

	unordered_set<const string*, pstr_hash_type, pstr_pred_type> all_values;
	unordered_map<const string*, unordered_set<const string*>*> cluster_map;
	unordered_set<unordered_set<const string*>*> clusters;
	unordered_set<const string*>* current = new unordered_set<const string*>();
	clusters.insert(current);

	// Read clusters
	string ln;
	while (getline(cin, ln))
	{
		if (!ln.size())
		{
			// End of the cluster
			if (current->size())
			{
				current = new unordered_set<const string*>();
				clusters.insert(current);
			}
			if (print_progress && (++cluster_count % interval == 0))
			{
				auto t_current = chrono::system_clock::now();
				auto t_delta = chrono::duration_cast<chrono::seconds>(t_current - t_start);
				unsigned long long t_count = static_cast<unsigned long long>(t_delta.count());
				fprintf(stderr,
					"%5llu:%02llu:%02llu  %12llu [%s]\n",
					(unsigned long long)(t_count / 3600u),
					(unsigned long long)((t_count / 60u) % 60u),
					(unsigned long long)(t_count % 60u),
					cluster_count, comment);
			}
		}
		else
		{
			auto p = all_values.find(&ln);
			if (p == all_values.end())
			{
				// New digest (does not combine)
				string* newstr = new string(ln);
				all_values.insert(newstr);
				current->insert(newstr);
				cluster_map[newstr] = current;
			}
			else
			{
				// Existing digest (combine)
				auto cluster_to_merge = cluster_map[*p];
				if (cluster_to_merge != current)
				{
					for (auto q : *current)
					{
						cluster_to_merge->insert(q);
						cluster_map[q] = cluster_to_merge;
					}
					clusters.erase(current);
					delete current;
					current = cluster_to_merge;
				}
			}
		}
	}

	for (auto p : clusters)
	{
		if (!p->size())
			continue;
		for (auto q : *p)
			puts(q->c_str());
		puts("");
	}

	if (print_progress)
	{
		auto t_current = chrono::system_clock::now();
		auto t_delta = chrono::duration_cast<chrono::seconds>(t_current - t_start);
		unsigned long long t_count = static_cast<unsigned long long>(t_delta.count());
		fprintf(stderr,
			"%5llu:%02llu:%02llu  %12llu [%s]\n",
			(unsigned long long)(t_count / 3600u),
			(unsigned long long)((t_count / 60u) % 60u),
			(unsigned long long)(t_count % 60u),
			cluster_count, comment);
	}

	return 0;
}
