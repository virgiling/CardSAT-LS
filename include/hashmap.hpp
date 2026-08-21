#ifndef _hashmap_hpp_INCLUDED
#define _hashmap_hpp_INCLUDED
typedef long long ll;

struct KeyValuePair{
	ll key;  
	int value;  
};

struct HashNode{
	KeyValuePair data;
	struct HashNode *next; 
};

struct HashMap{
	int size;
	HashNode **table;
	HashMap(int size = 10000007);
	~HashMap();
	int  get(ll key, int vsign);
	void erase(ll key);
	void insert(ll key, int value);
};


#endif