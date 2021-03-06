// shard.h

/*
   A "shard" is a database (replica pair typically) which represents
   one partition of the overall database.
*/

/**
*    Copyright (C) 2008 10gen Inc.
*
*    This program is free software: you can redistribute it and/or  modify
*    it under the terms of the GNU Affero General Public License, version 3,
*    as published by the Free Software Foundation.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "../pch.h"
#include "../client/dbclient.h"
#include "../client/model.h"
#include "../bson/util/atomic_int.h"
#include "shardkey.h"
#include "shard.h"

namespace mongo {

    class DBConfig;
    class Chunk;
    class ChunkRange;
    class ChunkManager;
    class ChunkRangeMangager;
    class ChunkObjUnitTest;

    typedef unsigned long long ShardChunkVersion;

    // key is max for each Chunk or ChunkRange
    typedef map<BSONObj,Chunk*,BSONObjCmp> ChunkMap;
    typedef map<BSONObj,shared_ptr<ChunkRange>,BSONObjCmp> ChunkRangeMap;
    
    /**
       config.chunks
       { ns : "alleyinsider.fs.chunks" , min : {} , max : {} , server : "localhost:30001" }
       
       x is in a shard iff
       min <= x < max
     */    
    class Chunk : public Model , boost::noncopyable {
    public:

        Chunk( ChunkManager * info );
        
        const BSONObj& getMin() const { return _min; }
        const BSONObj& getMax() const { return _max; }
        
        void setMin(const BSONObj& o){
            _min = o;
        }
        void setMax(const BSONObj& o){
            _max = o;
        }

        Shard getShard() const{
            return _shard;
        }
        void setShard( const Shard& shard );

        bool contains( const BSONObj& obj ) const;

        string toString() const;
        operator string() const { return toString(); }
        friend ostream& operator << (ostream& out, const Chunk& c){ return (out << c.toString()); }

        bool operator==(const Chunk& s) const;
        
        bool operator!=(const Chunk& s) const{
            return ! ( *this == s );
        }
        
        void getFilter( BSONObjBuilder& b ) const;
        BSONObj getFilter() const{ BSONObjBuilder b; getFilter( b ); return b.obj(); }
        
        // if min/max key is pos/neg infinity
        bool minIsInf() const;
        bool maxIsInf() const;

        BSONObj pickSplitPoint() const;
        Chunk * split();
        Chunk * split( const BSONObj& middle );

        /**
         * @return size of shard in bytes
         *  talks to mongod to do this
         */
        long getPhysicalSize() const;
        
        long countObjects( const BSONObj& filter = BSONObj() ) const;
        
        /**
         * if the amount of data written nears the max size of a shard
         * then we check the real size, and if its too big, we split
         */
        bool splitIfShould( long dataWritten );
        

        /*
         * moves either this shard or newShard if it makes sense too
         * @return whether or not a shard was moved
         */
        bool moveIfShould( Chunk * newShard = 0 );

        bool moveAndCommit( const Shard& to , string& errmsg );

        virtual const char * getNS(){ return "config.chunks"; }
        virtual void serialize(BSONObjBuilder& to);
        virtual void unserialize(const BSONObj& from);
        virtual string modelServer();
        
        void appendShortVersion( const char * name , BSONObjBuilder& b );

        virtual void save( bool check=false );
        
        void ensureIndex();
        
        void _markModified();
        
        static int MaxChunkSize;

        static string genID( const string& ns , const BSONObj& min );

        const ChunkManager* getManager() const { return _manager; }
        
    private:
        
        // main shard info
        
        ChunkManager * _manager;
        ShardKeyPattern skey() const;

        string _ns;
        BSONObj _min;
        BSONObj _max;
        Shard _shard;
        ShardChunkVersion _lastmod;

        bool _modified;
        
        // transient stuff

        long _dataWritten;

        // methods, etc..
        
        void _split( BSONObj& middle );

        friend class ChunkManager;
        friend class ShardObjUnitTest;
    };

    class ChunkRange{
    public:
        const ChunkManager* getManager() const{ return _manager; }
        Shard getShard() const{ return _shard; }

        const BSONObj& getMin() const { return _min; }
        const BSONObj& getMax() const { return _max; }

        // clones of Chunk methods
        bool contains(const BSONObj& obj) const;
        void getFilter( BSONObjBuilder& b ) const;
        BSONObj getFilter() const{ BSONObjBuilder b; getFilter( b ); return b.obj(); }
        long countObjects( const BSONObj& filter = BSONObj() ) const;

        ChunkRange(ChunkMap::const_iterator begin, const ChunkMap::const_iterator end)
            : _manager(begin->second->getManager())
            , _shard(begin->second->getShard())
            , _min(begin->second->getMin())
            , _max(prior(end)->second->getMax())
        {
            assert( begin != end );

            DEV while (begin != end){
                assert(begin->second->getManager() == _manager);
                assert(begin->second->getShard() == _shard);
                ++begin;
            }
        }

        // Merge min and max (must be adjacent ranges)
        ChunkRange(const ChunkRange& min, const ChunkRange& max)
            : _manager(min.getManager())
            , _shard(min.getShard())
            , _min(min.getMin())
            , _max(max.getMax())
        {
            assert(min.getShard() == max.getShard());
            assert(min.getManager() == max.getManager());
            assert(min.getMax() == max.getMin());
        }

        friend ostream& operator<<(ostream& out, const ChunkRange& cr){
            return (out << "ChunkRange(min=" << cr._min << ", max=" << cr._max << ", shard=" << cr._shard <<")");
        }

    private:
        const ChunkManager* _manager;
        const Shard _shard;
        const BSONObj _min;
        const BSONObj _max;
    };


    class ChunkRangeManager {
    public:
        const ChunkRangeMap& ranges() const { return _ranges; }

        void clear() { _ranges.clear(); }

        void reloadAll(const ChunkMap& chunks);
        void reloadRange(const ChunkMap& chunks, const BSONObj& min, const BSONObj& max);

        // Slow operation -- wrap with DEV
        void assertValid() const;

        ChunkRangeMap::const_iterator upper_bound(const BSONObj& o) const { return _ranges.upper_bound(o); }
        ChunkRangeMap::const_iterator lower_bound(const BSONObj& o) const { return _ranges.lower_bound(o); }

    private:
        // assumes nothing in this range exists in _ranges
        void _insertRange(ChunkMap::const_iterator begin, const ChunkMap::const_iterator end);

        ChunkRangeMap _ranges;
    };

    /* config.sharding
         { ns: 'alleyinsider.fs.chunks' , 
           key: { ts : 1 } ,
           shards: [ { min: 1, max: 100, server: a } , { min: 101, max: 200 , server : b } ]
         }
    */
    class ChunkManager {
    public:

        ChunkManager( DBConfig * config , string ns , ShardKeyPattern pattern , bool unique );
        virtual ~ChunkManager();

        string getns() const {
            return _ns;
        }
        
        int numChunks(){ rwlock lk( _lock , false ); return _chunks.size(); }
        Chunk* getChunk( int i ){ rwlock lk( _lock , false ); return _chunks[i]; }
        bool hasShardKey( const BSONObj& obj );

        Chunk& findChunk( const BSONObj& obj , bool retry = false );
        Chunk* findChunkOnServer( const Shard& shard ) const;
        
        ShardKeyPattern& getShardKey(){  return _key; }
        const ShardKeyPattern& getShardKey() const {  return _key; }
        bool isUnique(){ return _unique; }
        
        /**
         * makes sure the shard index is on all servers
         */
        void ensureIndex();

        /**
         * @return number of Chunk added to the vector
         */
        int getChunksForQuery( vector<shared_ptr<ChunkRange> >& chunks , const BSONObj& query );

        /**
         * @return number of Shards added to the set
         */
        int getShardsForQuery( set<Shard>& shards , const BSONObj& query );

        void getAllShards( set<Shard>& all );

        void save();

        string toString() const;
        operator string() const { return toString(); }

        ShardChunkVersion getVersion( const Shard& shard ) const;
        ShardChunkVersion getVersion() const;

        /**
         * this is just an increasing number of how many ChunkManagers we have so we know if something has been updated
         */
        unsigned long long getSequenceNumber(){
            return _sequenceNumber;
        }

        void drop();
        
    private:
        
        void _reload();
        void _load();
        
        DBConfig * _config;
        string _ns;
        ShardKeyPattern _key;
        bool _unique;
        
        vector<Chunk*> _chunks;
        map<string,unsigned long long> _maxMarkers;

        ChunkMap _chunkMap;
        ChunkRangeManager _chunkRanges;

        unsigned long long _sequenceNumber;
        
        RWLock _lock;

        // This should only be called from Chunk after it has been migrated
        void _migrationNotification(Chunk* c);

        friend class Chunk;
        friend class ChunkRangeManager; // only needed for CRM::assertValid()
        static AtomicUInt NextSequenceNumber;

        /**
         * @return number of Chunk matching the query or -1 for all chunks.
         */
        int _getChunksForQuery( vector<shared_ptr<ChunkRange> >& chunks , const BSONObj& query );
    };

    // like BSONObjCmp. for use as an STL comparison functor
    // key-order in "order" argument must match key-order in shardkey
    class ChunkCmp {
    public:
        ChunkCmp( const BSONObj &order = BSONObj() ) : _cmp( order ) {}
        bool operator()( const Chunk &l, const Chunk &r ) const {
            return _cmp(l.getMin(), r.getMin());
        }
        bool operator()( const Chunk *l, const Chunk *r ) const {
            return operator()(*l, *r);
        }

        // Also support ChunkRanges
        bool operator()( const ChunkRange &l, const ChunkRange &r ) const {
            return _cmp(l.getMin(), r.getMin());
        }
        bool operator()( const shared_ptr<ChunkRange> l, const shared_ptr<ChunkRange> r ) const {
            return operator()(*l, *r);
        }
    private:
        BSONObjCmp _cmp;
    };



} // namespace mongo
