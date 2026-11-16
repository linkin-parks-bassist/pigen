module when_value_tb;
    reg clk=0,reset=1;
    always #5 clk=~clk;
    reg [7:0] choice=0,logical_data=0,relation=0;
    reg choice_valid=0,logical_data_valid=0,relation_valid=0;
    wire choice_ready,logical_data_ready,relation_ready;
    wire [7:0] selected;
    wire logical_result,relation_result,selected_valid,logical_result_valid,relation_result_valid;
    reg selected_ready=0,logical_result_ready=0,relation_result_ready=0;
    wire [15:0] accepted;
    when_value dut(.*);
    integer sent[0:2],got[0:2],cycle=0,checks;
    reg [7:0] expected[0:2][0:511],held[0:2];
    reg stalled[0:2];
    initial begin
        checks=$fopen("checks.csv","w");$fwrite(checks,"index,expected,observed\n");
        for(integer j=0;j<3;j=j+1)begin sent[j]=0;got[j]=0;stalled[j]=0;end
        repeat(5)@(negedge clk);reset=0;
    end
    always @(negedge clk)if(!reset)begin
        choice=sent[0];choice_valid=sent[0]<512;
        logical_data=sent[1];logical_data_valid=sent[1]<512;
        relation=sent[2];relation_valid=sent[2]<512;
        selected_ready=cycle>18 && cycle%7!=0;
        logical_result_ready=cycle>11 && cycle%5>1;
        relation_result_ready=cycle>23 && cycle%11>4;
    end
    always @(posedge clk)if(!reset)begin : scoreboard
        reg [7:0] data_out[0:2];reg [2:0] valid_out,ready_out,fire_in;
        data_out[0]=selected;data_out[1]={7'b0,logical_result};data_out[2]={7'b0,relation_result};
        valid_out={relation_result_valid,logical_result_valid,selected_valid};ready_out={relation_result_ready,logical_result_ready,selected_ready};
        fire_in={relation_valid&&relation_ready,logical_data_valid&&logical_data_ready,choice_valid&&choice_ready};
        cycle=cycle+1;
        for(integer j=0;j<3;j=j+1)begin
            if(stalled[j]&&(!valid_out[j]||data_out[j]!==held[j]))$fatal(1,"when value changed while stalled");
            if(fire_in[j])begin
                case(j)
                    0:expected[j][sent[j]]=(choice[0]?(choice+1):(choice^8'ha5));
                    1:expected[j][sent[j]]=(logical_data<=127 || logical_data==255);
                    2:expected[j][sent[j]]=(relation<=127);
                endcase
                sent[j]=sent[j]+1;
            end
            if(valid_out[j]&&ready_out[j])begin
                if(got[j]>=sent[j]||data_out[j]!==expected[j][got[j]])$fatal(1,"when precedence channel=%0d index=%0d got=%h expected=%h",j,got[j],data_out[j],expected[j][got[j]]);
                $fwrite(checks,"%0d,%0d,%0d\n",j*512+got[j],expected[j][got[j]],data_out[j]);got[j]=got[j]+1;
            end
            stalled[j]=valid_out[j]&&!ready_out[j];held[j]=data_out[j];
        end
        #1;if(accepted!==sent[0])$fatal(1,"when body count %0d expected %0d",accepted,sent[0]);
        if(got[0]==512&&got[1]==512&&got[2]==512)begin $display("PASS when value precedence: 1536 ternary/logical/relational results, independent stalls and same-fire body count");$finish;end
    end
    initial begin #100000;$fatal(1,"when value timeout");end
endmodule
