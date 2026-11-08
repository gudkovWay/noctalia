--!nonstrict
local width,height=104,671
local function draw()
  barWidget.render(ui.box({key='mina-sidebar-material',width=width,height=height,fill='#00000000',inputRects={0,0,0,0}}, {
    ui.image({key='sleeve',path='skin-assets/mina/sidebar-frame.svg',x=0,y=0,width=width,height=height,fit='stretch'}),
  }))
end
function onBarGeometry(w,h) width,height=w,h;draw() end
draw()
