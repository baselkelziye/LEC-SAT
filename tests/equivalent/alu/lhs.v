// alu4.v
module lhs (
    input  wire [3:0] a,
    input  wire [3:0] b,
    input  wire [1:0] op,     // 00=add, 01=sub, 10=and, 11=or
    output wire [3:0] result,
    output wire       cout
);

    wire [4:0] sum_ext  = {1'b0, a} + {1'b0, b};
    wire [4:0] diff_ext = {1'b0, a} - {1'b0, b};

    reg [3:0] res;
    reg       co;

    always @(*) begin
        case (op)
            2'b00: begin res = sum_ext[3:0];  co = 1'b0;  end //cout is not correct
            2'b01: begin res = diff_ext[3:0]; co = diff_ext[4]; end
            2'b10: begin res = a & b;         co = 1'b0;        end
            2'b11: begin res = a | b;         co = 1'b0;        end
        endcase
    end

    assign result = res;
    assign cout   = co;

endmodule