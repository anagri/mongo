/* hashtab.h

   Simple, fixed size hash table.  Darn simple.

   Uses a contiguous block of memory, so you can put it in a memory mapped file very easily.
*/

/*    Copyright 2009 10gen Inc.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#pragma once

#include "../pch.h"
#include <map>

namespace mongo {

#pragma pack(1)

    /* you should define:

       int Key::hash() return > 0 always.
    */

    template <
    class Key,
    class Type,
    class PTR
    >
    class HashTable : boost::noncopyable {
    public:
        const char *name;
        struct Node {
            int hash;
            Key k;
            Type value;
            bool inUse() {
                return hash != 0;
            }
            void setUnused() {
                hash = 0;
            }
        };
        PTR _buf;
        int n;
        int maxChain;

        Node& nodes(int i) {
            return *((Node*) _buf.at(i * sizeof(Node), sizeof(Node)));
        }

        int _find(const Key& k, bool& found) {
            found = false;
            int h = k.hash();
            int i = h % n;
            int start = i;
            int chain = 0;
            int firstNonUsed = -1;
            while ( 1 ) {
                if ( !nodes(i).inUse() ) {
                    if ( firstNonUsed < 0 )
                        firstNonUsed = i;
                }

                if ( nodes(i).hash == h && nodes(i).k == k ) {
                    if ( chain >= 200 )
                        out() << "warning: hashtable " << name << " long chain " << endl;
                    found = true;
                    return i;
                }
                chain++;
                i = (i+1) % n;
                if ( i == start ) {
                    // shouldn't get here / defensive for infinite loops
                    out() << "error: hashtable " << name << " is full n:" << n << endl;
                    return -1;
                }
                if( chain >= maxChain ) { 
                    if ( firstNonUsed >= 0 )
                        return firstNonUsed;
                    out() << "error: hashtable " << name << " max chain n:" << n << endl;
                    return -1;
                }
            }
        }

    public:
        /* buf must be all zeroes on initialization. */
        HashTable(PTR buf, int buflen, const char *_name) : name(_name) {
            int m = sizeof(Node);
            // out() << "hashtab init, buflen:" << buflen << " m:" << m << endl;
            n = buflen / m;
            if ( (n & 1) == 0 )
                n--;
            maxChain = (int) (n * 0.05);
            _buf = buf;
            //nodes = (Node *) buf;

            assert( sizeof(Node) == 628 );
            //out() << "HashTable() " << _name << " sizeof(node):" << sizeof(Node) << " n:" << n << endl;
        }

        Type* get(const Key& k) {
            bool found;
            int i = _find(k, found);
            if ( found )
                return &nodes(i).value;
            return 0;
        }

        void kill(const Key& k) {
            bool found;
            int i = _find(k, found);
            if ( i >= 0 && found ) {
                Node& n = nodes(i);
                n.k.kill();
                n.setUnused();
            }
        }
/*
        void drop(const Key& k) {
            bool found;
            int i = _find(k, found);
            if ( i >= 0 && found ) {
                nodes[i].setUnused();
            }
        }
*/
        /** returns false if too full */
        bool put(const Key& k, const Type& value) {
            bool found;
            int i = _find(k, found);
            if ( i < 0 )
                return false;
            Node& n = nodes(i);
            if ( !found ) {
                n.k = k;
                n.hash = k.hash();
            }
            else {
                assert( n.hash == k.hash() );
            }
            n.value = value;
            return true;
        }
        
        typedef void (*IteratorCallback)( const Key& k , Type& v );
        void iterAll( IteratorCallback callback ){
            for ( int i=0; i<n; i++ ){
                if ( ! nodes(i).inUse() )
                    continue;
                callback( nodes(i).k , nodes(i).value );
            }
        }

        // TODO: should probably use boost::bind for this, but didn't want to look at it
        typedef void (*IteratorCallback2)( const Key& k , Type& v , void * extra );
        void iterAll( IteratorCallback2 callback , void * extra ){
            for ( int i=0; i<n; i++ ){
                if ( ! nodes(i).inUse() )
                    continue;
                callback( nodes(i).k , nodes(i).value , extra );
            }
        }
    
    };

#pragma pack()

} // namespace mongo
