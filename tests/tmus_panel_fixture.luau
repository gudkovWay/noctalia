testCalls, testWatches = {}, {}
testClock, testClosed = 1000, false
testState = {
 linked=true,status='paused',title='Night Service',artist='Owned fixture',artists={'Owned fixture'},
 trackId='ytmusic:night',artPath='',position=84,duration=212,volume=45,loop='none',shuffle=false,
 source='all',searchSerial=1,error='',tracksId='local:night',tracksLoading=false,hasMore=false,
 providers={
  {id='ytmusic',name='YT Music',authKind='Ready',authHint='Browser session',caps={radio=true,playlist_add=true}},
  {id='soundcloud',name='SoundCloud',authKind='Anonymous',authHint='Public catalogue',caps={radio=true}},
  {id='missing',name='Missing session',authKind='Expired',authHint='Sign in in your browser',caps={}}
 },
 playlists={
  {id={provider='local',id='night'},title='Night Service',track_count=12},
  {id={provider='local',id='liked'},title='Liked Music',track_count=1},
 },
 tracks={},results={},queue={},queueOrder={2,0,1,3},queueIndex=0,ratings={},
 equalizer={enabled=false,preset='Flat',bands={0,0,0,0,0,0,0,0,0,0}}
}
for i=1,12 do
 local track={id={provider=i%2==0 and 'soundcloud' or 'ytmusic',id='track-'..i},
  title=({'Night Service','Neon Rain','Quiet Streets','Red Window','Slow Transit','After Hours'})[(i-1)%6+1],
  artists={'Owned fixture'},duration=180+i*7,art_path='',album='Isolated catalogue'}
 testState.tracks[i]=track
 testState.results[i]=table.clone(track);testState.results[i].kind='track'
 if i<=4 then testState.queue[i]=track end
end
testState.queue[1].id={provider='ytmusic',id='night'}
testState.queue[3].id={provider='ytmusic',id='night'}
testHome={status='ok',shelves={
 {title='Ночная смена',subtitle='Изолированный стенд',items=testState.results},
 {title='Рекомендации',subtitle='Изолированный стенд',items=testState.results}
}}
noctalia=setmetatable({
 nowMs=function() return testClock end,
 state={get=function(key) if key=='tmus.home' then return testHome else return testState end end,
  watch=function(key,cb) testWatches[key]=cb end},
 clipboardText=function() return 'fixture query' end,
 runAsync=function(argv,cb)
  table.insert(testCalls,argv)
  print('MUSIC_FIXTURE_REQUEST '..(type(argv)=='table' and table.concat(argv,' ') or argv))
  if cb then cb({exitCode=0,stdout='',stderr=''}) end
  return true
 end,
}, {__index=noctalia})
function testSent(event,payload)
 for _,argv in ipairs(testCalls) do
  if type(argv)=='table' and argv[6]==event and (payload==nil or argv[7]==payload) then return true end
 end
 return false
end
function testPublish()
 testWatches.tmus(testState)
end
