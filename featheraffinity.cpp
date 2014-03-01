#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <string>
#include <list>
#include <unistd.h>
#include <curl/curl.h>
#include <htmlcxx/html/ParserDom.h>

extern "C" {
#include <oauth.h>
}

using namespace std;
using namespace htmlcxx;

enum loglevel {LOG_FAIL, LOG_WARN, LOG_NOTE};

struct SubmissionData {
	int id;
	string title;
	string username;
	bool sfw;
	list<string> tags;
	string comment;
};

int writer(char *data, size_t size, size_t nmemb, string *out);
string htmlContent(string page, tree<HTML::Node>::iterator it);
bool faLogin();
string getSubmission(const char *subfile);
SubmissionData scrapeSubmissionData(string page);
bool checkSubmission(const char *blockfile, const char *requirefile, SubmissionData data);
string buildStatus(SubmissionData data);
bool tweet(string status);
void log(loglevel level, string msg);

//Filenames
const char *configFile = "featherconfig.cfg";
const char *cookieFile = "FACookie.tsv";
const char *submissionsFile = "submissions.csv";
const char *blockFile = "block.csv";
const char *requireFile = "require.csv";

//Settings from config file
loglevel logLevel;
char faUsername[255];
char faPassword[255];
char consumerKey[255];
char consumerSecret[255];
char oauthToken[255];
char oauthTokenSecret[255];

int main(int argc, char * argv[]) {
	//Get account data from config file
	FILE * config = fopen(configFile, "r");
	if(config == NULL) {
		log(LOG_FAIL, "Opening config file failed!");
		return EXIT_FAILURE;
	}
	fscanf(config, "%*[^=]=%254[^\n]\n", faUsername);
	fscanf(config, "%*[^=]=%254[^\n]\n", faPassword);
	fscanf(config, "%*[^=]=%254[^\n]\n", consumerKey);
	fscanf(config, "%*[^=]=%254[^\n]\n", consumerSecret);
	fscanf(config, "%*[^=]=%254[^\n]\n", oauthToken);
	fscanf(config, "%*[^=]=%254[^\n]\n", oauthTokenSecret);
	fscanf(config, "%*[^=]=%d\n", (int*)&logLevel);
	fclose(config);

	while(!faLogin()) {
		sleep(5); //Attempt to login to FA, retrying every 5 seconds on failure
	}

	int delay = 0;
	int lastDelay = 0;

	while(true) {
		string submission = getSubmission(submissionsFile); //Find random valid submission ID and store in submissions.csv
		if(submission.empty()) {
			continue;
		}
		SubmissionData subData = scrapeSubmissionData(submission); //Find tags and audience target from submission, store in struct
		if(!checkSubmission(blockFile, requireFile, subData)) { //Check if submission complies with tag/artist/nsfw block rules, try again if not
			continue;
		}
		while(!tweet(buildStatus(subData))) { //Build tweet from data and tweet it, retrying every 5 seconds on failure
			sleep(5);
		}
		delay = rand() % 100 + 20; //Generates a number from 20 to 120 in minutes
		log(LOG_NOTE, "Delay: " + to_string(delay) + " minutes.");
		log(LOG_NOTE, "Sleeping for " + to_string((120 - lastDelay) + delay) + " minutes.");
		sleep(((120 - lastDelay) + delay) * 60); //Waits until end of last 2 hour period plus the time of the new delay
		lastDelay = delay;
	}
	return EXIT_SUCCESS;
}

int writer(char *data, size_t size, size_t nmemb, string *out) {
	if(out == NULL) {
		return 0;
	}
	out->append(data, size * nmemb);
	return size * nmemb;
}

string htmlContent(string page, tree<HTML::Node>::iterator it) {
	return page.substr(it->offset() + it->text().length(), it->length() - it->text().length() - it->closingText().length());
}

bool faLogin() {
	CURL *curl = curl_easy_init();
	if(curl) {
		string loginFields = "action=login&retard_protection=1";
		loginFields += "&name=";
		loginFields += curl_easy_escape(curl, faUsername, 0);
		loginFields += "&pass=";
		loginFields += curl_easy_escape(curl, faPassword, 0);
		loginFields += "&login=Login+to%C2%A0FurAffinity";

		curl_easy_setopt(curl, CURLOPT_URL, "https://www.furaffinity.net/login/");
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, loginFields.c_str());
		curl_easy_setopt(curl, CURLOPT_COOKIELIST, ".furaffinity.net\tTRUE\t/\tFALSE\t2147483647\tsfw\t0");
		curl_easy_setopt(curl, CURLOPT_COOKIEFILE, cookieFile);
		curl_easy_setopt(curl, CURLOPT_COOKIEJAR, cookieFile);

		if(curl_easy_perform(curl) != CURLE_OK) {
			log(LOG_FAIL, "FA login failed!");
			return false;
		}
	} else {
		log(LOG_FAIL, "FA login failed!");
		return false;
	}
	curl_easy_cleanup(curl);
	log(LOG_NOTE, "FA login succeeded.");
	return true;
}

string getSubmission(const char *subfile) {
	//Get front page of FA to find latest submission ID
	string frontPage;
	CURL *curl = curl_easy_init();
	if(!curl) {
		log(LOG_FAIL, "Getting FA front page failed!");
		return NULL;
	}
	curl_easy_setopt(curl, CURLOPT_COOKIEFILE, cookieFile);
	curl_easy_setopt(curl, CURLOPT_COOKIEJAR, cookieFile);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writer);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &frontPage);

	curl_easy_setopt(curl, CURLOPT_URL, "https://www.furaffinity.net/");
	if(curl_easy_perform(curl) != CURLE_OK) {
		log(LOG_FAIL, "Getting FA front page failed!");
		return NULL;
	}
	curl_easy_cleanup(curl);

	//Scrape latest submission ID from page
	int latestSubID = -1;
	HTML::ParserDom parser;
	tree<HTML::Node> dom = parser.parseTree(frontPage);
	tree<HTML::Node>::iterator it = dom.begin();
	tree<HTML::Node>::iterator end = dom.end();
	for(; it != end; ++it) {
		if(it->tagName() == "a") {
			it->parseAttributes();
			if(it->attribute("href").first && it->attribute("href").second.substr(0, 6).compare("/view/") == 0) {
				latestSubID = atoi(it->attribute("href").second.substr(6).c_str());
				if(latestSubID != -1) {
					break;
				}
			}
		}
	}

	int id = -1;
	string submissionPage;
	srand(time(NULL));
	do {
		//Get random ID
		id = rand() % latestSubID + 1;

		//Check if submission has already been posted, get new ID if so
		FILE * submissions = fopen(subfile, "a+");
		rewind(submissions);
		char buffer[10];
		while(!feof(submissions)) {
			fscanf(submissions, "%[^,],\n", buffer);
			if(id == atoi(buffer)) {
				id = -1;
			}
		}
		if(id == -1) {
			fclose(submissions);
			continue;
		}
		fprintf(submissions, "%d,\n", id);
		fclose(submissions);

		//Get page for selected submission
		CURL *curlSubmission = curl_easy_init();
		if(curlSubmission) {
			string url = "https://www.furaffinity.net/view/" + to_string(id);
			curl_easy_setopt(curlSubmission, CURLOPT_URL, url.c_str());
			curl_easy_setopt(curlSubmission, CURLOPT_COOKIEFILE, cookieFile);
			curl_easy_setopt(curlSubmission, CURLOPT_COOKIEJAR, cookieFile);
			curl_easy_setopt(curlSubmission, CURLOPT_WRITEFUNCTION, writer);
			curl_easy_setopt(curlSubmission, CURLOPT_WRITEDATA, &submissionPage);
			if(curl_easy_perform(curlSubmission) != CURLE_OK) {
				log(LOG_FAIL, "Getting submission page failed!");
				id = -1;
			}
		} else {
			log(LOG_FAIL, "Getting submission page failed!");
			id = -1;
		}
		curl_easy_cleanup(curlSubmission);

		//Make sure submission exists, get new ID otherwise
		HTML::ParserDom parser;
		tree<HTML::Node> dom = parser.parseTree(submissionPage);
		tree<HTML::Node>::iterator it = dom.begin();
		tree<HTML::Node>::iterator end = dom.end();
		for(; it != end; ++it) {
			if(it->tagName() == "title") {
				it->parseAttributes();
				if(htmlContent(submissionPage, it).compare("System Error") == 0) {
					id = -1; //If page is not a valid submission, get a new ID
					submissionPage = ""; //Reset submission page for next run of loop
					log(LOG_WARN, "Invalid submission id.");
					break;
				}
			} else if(it->tagName() == "b") {
				it->parseAttributes();
				if(htmlContent(submissionPage, it).compare(" has elected to make their content available to registered users only.") == 0) {
					id = -1; //If page is not a valid submission, get a new ID
					submissionPage = ""; //Reset submission page for next run of loop
					log(LOG_WARN, "Invalid submission id.");
					break;
				}
			}
		}
	} while(id == -1);

	log(LOG_NOTE, "Submission page retrieved successfully.");
	return submissionPage;
}

SubmissionData scrapeSubmissionData(string page) {
	SubmissionData data;

	//Scrape all useful data from the submission page at once for speed
	HTML::ParserDom parser;
	tree<HTML::Node> dom = parser.parseTree(page);
	tree<HTML::Node>::iterator it = dom.begin();
	tree<HTML::Node>::iterator end = dom.end();
	for(; it != end; ++it) {
		if(it->tagName() == "a") {
			it->parseAttributes();
			if(it->attribute("href").first && it->attribute("href").second.substr(0, 6).compare("/user/") == 0 && !it->attribute("id").first && data.username.empty()) { //"/user/" is not unique, check for first without id="my-username"
				data.username = htmlContent(page, it); //Username
			} else if(it->attribute("href").first && it->attribute("href").second.substr(0, 6).compare("/full/") == 0) {
				data.id = atoi(it->attribute("href").second.substr(6).c_str()); //ID
			}
		}
		if(it->tagName() == "img") {
			it->parseAttributes();
			if(it->attribute("id").first && it->attribute("id").second.compare("submissionImg") == 0) {
				data.title = it->attribute("alt").second; //Title
			} else if(it->attribute("src").first && it->attribute("src").second.substr(0, 12).compare("/img/labels/") == 0) {
				if(it->attribute("src").second.compare("/img/labels/general.gif") == 0) {
					data.sfw = true; //Rating
				} else {
					data.sfw = false;
				}
			}
		}
		if(it->tagName() == "div") {
			it->parseAttributes();
			if(it->attribute("id").first && it->attribute("id").second.compare("keywords") == 0) {
				string keyHTML = htmlContent(page, it);
				HTML::ParserDom keyParser;
				tree<HTML::Node> keywords = keyParser.parseTree(keyHTML);
				tree<HTML::Node>::iterator itKey = keywords.begin();
				tree<HTML::Node>::iterator endKey = keywords.end();
				for(; itKey != endKey; ++itKey) {
					if(itKey->tagName() == "a") {
						data.tags.push_back(htmlContent(keyHTML, itKey)); //Individual tags
					}
				}
			}
		}
		if(it->tagName() == "td") {
			it->parseAttributes();
			if(it->attribute("class").first && it->attribute("class").second.substr(0, 15).compare("replyto-message") == 0) {
				data.comment = htmlContent(page, it); //First comment (retains HTML formatting)
				break; //Comments are at the bottom of the page, so don't process anything else
			}
		}
	}

	log(LOG_NOTE, "Submission data scraped successfully.");
	return data;
}

bool checkSubmission(const char *blockfile, const char *requirefile, SubmissionData data) {
	//Add username and rating to list of tags to be checked
	data.tags.push_front(data.username);
	data.tags.push_front(data.sfw ? "sfw" : "nsfw");

	//Check if submission contains any blocked tags
	FILE * blockedIdentifiers = fopen(blockfile, "r");
	if(blockedIdentifiers != NULL) { //Blockfile exists, so check tags against it
		char curtag[255];
		while(!feof(blockedIdentifiers)) {
			fscanf(blockedIdentifiers, "%[^,],\n", curtag);
			//Compare everything
			for(list<string>::iterator it = data.tags.begin(); it != data.tags.end(); ++it) {
				if(strcasecmp(it->c_str(), curtag) == 0) {
					fclose(blockedIdentifiers);
					log(LOG_WARN, "Submission did not satisfy block and/or require criteria.");
					return false;
				}
			}
		}
		fclose(blockedIdentifiers);
	}

	//Check if submission is missing any required tags
	FILE * requiredIdentifiers = fopen(requirefile, "r");
	if(requiredIdentifiers != NULL) { //Requirefile exists, so check tags against it
		char curtag[255];
		while(!feof(requiredIdentifiers)) {
			fscanf(requiredIdentifiers, "%[^,],\n", curtag);
			//Compare everything
			bool tagMatch = false;
			for(list<string>::iterator it = data.tags.begin(); it != data.tags.end(); ++it) {
				if(strcasecmp(it->c_str(), curtag) == 0) {
					tagMatch = true;
					break;
				}
			}
			if(!tagMatch) {
				fclose(requiredIdentifiers);
				log(LOG_WARN, "Submission did not satisfy block and/or require criteria.");
				return false;
			}
		}
		fclose(requiredIdentifiers);
	}

	log(LOG_NOTE, "Submission satisfies block and require criteria.");
	return true;
}

string buildStatus(SubmissionData data) {
	string status;
	unsigned int availableChars = 117; //t.co link shortening is automatic
	if(data.sfw) {
		availableChars -= 6;
	} else {
		availableChars -= 7;
	}
	if(data.username.size() <= 20) {
		availableChars -= 4 + data.username.size();
	} else {
		data.username = data.username.substr(0, 17) + "...";
		availableChars -= 4 + 20;
	}
	if(data.title.size() < availableChars) {
		status = data.title + " by " + data.username + (data.sfw ? " [SFW]" : " [NSFW]") + " https://furaffinity.net/view/" + to_string(data.id);
	} else {
		status = data.title.substr(0, availableChars) + "... by " + data.username + (data.sfw ? " [SFW]" : " [NSFW]") + " https://furaffinity.net/view/" + to_string(data.id);
	}
	return status;
}

bool tweet(string status) {
	CURL *curl = curl_easy_init();
	string response;
	if(curl) {
		char *postData = NULL;
		string url = "https://api.twitter.com/1.1/statuses/update.json?status=";
		url += curl_easy_escape(curl, status.c_str(), 0);
		char *oauthURL = oauth_sign_url2(url.c_str(), &postData, OA_HMAC, NULL, consumerKey, consumerSecret, oauthToken, oauthTokenSecret);

		curl_easy_setopt(curl, CURLOPT_URL, oauthURL);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writer);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

		if(curl_easy_perform(curl) != CURLE_OK) {
			log(LOG_FAIL, "Posting tweet failed!");
			return false;
		}
	} else {
		log(LOG_FAIL, "Posting tweet failed!");
		return false;
	}
	curl_easy_cleanup(curl);
	log(LOG_NOTE, "Twitter response: " + response);
	log(LOG_NOTE, "Tweet posted successfully.");
	return true;
}

void log(loglevel level, string msg) {
	if(level > logLevel) {
		return;
	}

	string levelName = "    ";
	if(level == 0) {
		levelName = "FAIL";
	} else if(level == 1) {
		levelName = "WARN";
	} else if(level == 2) {
		levelName = "NOTE";
	}
	printf("[%ld]\t[%s]\t%s\n", time(NULL), levelName.c_str(), msg.c_str());
}