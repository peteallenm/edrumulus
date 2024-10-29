
function SubmitEditable()
{
	
}

function loadTable(Table)
{
    var JsonUrl = "status/" + Table + ".json";
    var UpdateRate = 5000;
    
    //console.log("Loading " + Table);
    
    $.getJSON( JsonUrl, function( data ) 
    {
        var TableName = data.TableName ;
        UpdateRate = data.UpdateRate;
        var TableColumns = data.Columns;
        var HasEditable = false;
        //console.log(data);
        //console.log("Table : " + TableName + ", Update rate: " + UpdateRate + " Columns: " + TableColumns);
        
        var Html = "<tr>\n";
        var Col = 0;
        
        $.each( data.Elements, function( num, obj ) {
            $.each( obj, function( key, val ) {
                //console.log(key + ": [" + val[0] + "] id = [" + val[1] + "] editable = [" + val[2] + "]");
                
                Html += "  <td><strong>"  + key + "</strong>: ";
                if(0 == val[2])
                {
                    Html += val[0];
                }
                else
                {
                    HasEditable = true;
			
                    Html += '<input class="inp-text" name="' + val[1] + '" id="' + val[1] + '" type="text" value="'+ val[0] + '" size="'  + (val[0].length + 2) + '"/>';
                }
                Html += "</td>\n";
                
                Col ++;
                if(Col >= TableColumns)
                {
                    Col = 0;
                    Html += "\n</tr>\n<tr>\n";
                 }
            });
        });
        
        if(true == HasEditable)
        {
            Html += '<td> <button id="' + TableName + 'Btn">Submit</button>  </td>' + "\n";
            Html += '<script> $("button").click(function(){' + "\n";
            //Html += '$.post("index.html", { name: "John", time: "2pm" } );
            Html += '$.post( "index.html", $( "#testform" ).serialize() )';
        }
        $('#' + Table).html(Html);
        
       //console.log("Reload: " + Table + ", UpdateRate: " + UpdateRate);
       setTimeout(loadTable, UpdateRate, Table);
    })
    .fail(function(jqXHR, textStatus, errorThrown) 
    { 
	console.log('new getJSON request failed! ' + textStatus); 
	    
       setTimeout(loadTable, 5000, Table);
   });
}


      